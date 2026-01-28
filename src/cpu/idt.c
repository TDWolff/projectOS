#include "idt.h"
#include "task.h"
#include "../drivers/vga.h"
#include "../drivers/timer.h"
#include "../drivers/keyboard.h"
#include "../lib/stdio.h"
#include "../include/ports.h"
#include "../drivers/mouse.h"
#include "../mem/vmm.h"
#include "../fs/initrd.h"  // Added for file syscalls
#include "../lib/string.h" // Added for memcpy

void pic_remap(); 

__attribute__((aligned(0x10)))
static idt_entry_t idt[256];
static idtr_t idtr;

const char *exception_messages[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
    "Coprocessor Fault", "Alignment Check", "Machine Check", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved"
};

extern void isr0(); extern void isr8(); extern void isr13();
extern void isr14(); extern void isr32(); extern void isr33(); extern void isr44(); 
extern void isr128(); // Changed from isr0x80
extern void load_idt(idtr_t*);
void syscall_handler(registers_t* regs);

void idt_set_gate(uint8_t num, uint64_t base) {
    idt[num].isr_low = (uint16_t)(base & 0xFFFF);
    idt[num].kernel_cs = 0x08;
    idt[num].ist = 0;
    idt[num].attributes = 0x8E; 
    idt[num].isr_mid = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].isr_high = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    idt[num].reserved = 0;
}

// Same as idt_set_gate, but mark the descriptor as user-callable (DPL=3)
void idt_set_gate_user(uint8_t num, uint64_t base) {
    idt[num].isr_low = (uint16_t)(base & 0xFFFF);
    idt[num].kernel_cs = 0x08;
    idt[num].ist = 0;
    // 0xEE = P=1, DPL=3, type=0xE (64-bit interrupt gate)
    idt[num].attributes = 0xEE;
    idt[num].isr_mid = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].isr_high = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    idt[num].reserved = 0;
}

void idt_init() {
    idtr.limit = (uint16_t)sizeof(idt_entry_t) * 256 - 1;
    idtr.base = (uint64_t)&idt;

    idt_set_gate(0, (uint64_t)isr0);
    idt_set_gate(8, (uint64_t)isr8);
    idt_set_gate(13, (uint64_t)isr13);
    idt_set_gate(14, (uint64_t)isr14);
    idt_set_gate(32, (uint64_t)isr32);
    idt_set_gate(33, (uint64_t)isr33);
    idt_set_gate(44, (uint64_t)isr44);
    // 0x80: syscall entry, must be callable from user space (DPL=3)
    idt_set_gate_user(0x80, (uint64_t)isr128);

    pic_remap();
    load_idt(&idtr);
}

// The core "Blue Screen" function
void kpanic(registers_t* regs, const char* reason) {
    __asm__ volatile("cli"); // Disable interrupts immediately

    terminal_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE); // Classic BSOD color
    terminal_clear();

    kprintf(" :(  A problem has been detected and ProjectOS has been shut down.\n\n");
    kprintf("KERNEL_PANIC: %s\n", reason);
    
    if (regs) {
        kprintf("Exception: %d (%s)  Error Code: %x\n", regs->int_no, exception_messages[regs->int_no], regs->err_code);
        kprintf("RIP: %x   CS: %x   RFLAGS: %x\n", regs->rip, regs->cs, regs->rflags);
        kprintf("RSP: %x   SS: %x   RBP: %x\n\n", regs->rsp, regs->ss, regs->rbp);
        
        // General Purpose Registers
        kprintf("RAX: %x  RBX: %x  RCX: %x\n", regs->rax, regs->rbx, regs->rcx);
        kprintf("RDX: %x  RSI: %x  RDI: %x\n", regs->rdx, regs->rsi, regs->rdi);
        kprintf("R8:  %x  R9:  %x  R10: %x\n", regs->r8,  regs->r9,  regs->r10);
    }

    kprintf("\nTechnical Information:\n");
    kprintf("*** STOP: 0x0000000%x\n\n", regs ? regs->int_no : 0);
    kprintf("The system has been halted. Please restart your computer.");

    while(1) { __asm__ volatile("hlt"); }
}

uint64_t isr_handler(uint64_t* stack_ptr) {
    registers_t* regs = (registers_t*)stack_ptr;
    uint64_t return_rsp = (uint64_t)stack_ptr;

    // Send EOI immediately
    if (regs->int_no >= 32) {
        if (regs->int_no >= 40) outb(0xA0, 0x20);
        outb(0x20, 0x20);
    }

    if (regs->int_no == 0x80) {
        syscall_handler(regs);
    } 

    if (regs->int_no < 32) {
        kpanic(regs, "CPU_EXCEPTION");
    } 
    
    if (regs->int_no == 32) {
        timer_handler();
        // Only return a new task stack if it's the timer
        return schedule((uint64_t)stack_ptr);
    } 
    
    if (regs->int_no == 33) {
        keyboard_handler();
    } 
    
    if (regs->int_no == 44) {
        mouse_handler();
        return (uint64_t)stack_ptr;
    }

    return return_rsp;
}

void pic_remap() {
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    outb(0x21, 0xF8); // Timer(0), Keyboard(1), SlaveBridge(2)
    outb(0xA1, 0xEF); // Mouse(12)
}

void syscall_handler(registers_t* regs) {
    // The user app puts the syscall number in RAX
    // Arguments are passed in RDI, RSI, RDX, etc.
    switch (regs->rax) {
        case 1: // Syscall 1: kprintf
            kprintf((const char*)regs->rdi);
            break;
            
        case 5: // Syscall 5: get_fb_info
            video_get_info((fb_info_t*)regs->rdi);
            break;
            
        case 10: // Syscall 10: get_key
            // Return scan code/char in RAX. 
            // Since syscall_handler returns void, we mod regs->rax directly
            regs->rax = (uint64_t)keyboard_get_key();
            break;

        case 60: // Syscall 60: exit
            kprintf("\n[Process Exited with code %d]\n", regs->rdi);
            break;

        case 20: // Syscall 20: list_files
            // rdi = pointer to user buffer for file_t array
            if (regs->rdi) {
                file_t* files = initrd_get_files();
                // Copy MAX_FILES * sizeof(file_t) to user buffer
                memcpy((void*)regs->rdi, files, sizeof(file_t) * MAX_FILES);
            }
            break;

        case 21: // Syscall 21: read_file
            // rdi = filename, rsi = buffer, rdx = max_size
            // returns bytes read in rax, or -1 if not found
            if (regs->rdi && regs->rsi) {
                char* filename = (char*)regs->rdi;
                file_t* f = initrd_open(filename);
                if (f) {
                    uint64_t bytes_to_copy = f->size;
                    if (bytes_to_copy > regs->rdx) bytes_to_copy = regs->rdx;
                    
                    // Dangerous: copying directly to user pointer without check, but okay for now
                    memcpy((void*)regs->rsi, (void*)f->address, bytes_to_copy);
                    regs->rax = bytes_to_copy;
                } else {
                     regs->rax = -1; // Error
                }
            }
            break;

        default:
            kprintf("Unknown syscall: %d\n", regs->rax);
            break;
    }
}