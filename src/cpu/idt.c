#include "idt.h"
#include "../drivers/timer.h"
#include "../drivers/keyboard.h"
#include "../lib/stdio.h"
#include "../drivers/vga.h"
#include "../include/ports.h"

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

extern void isr0(); extern void isr1(); extern void isr8();
extern void isr13(); extern void isr14(); extern void isr32();
extern void isr33();
extern void load_idt(idtr_t*);

void idt_set_gate(uint8_t num, uint64_t base) {
    idt[num].isr_low = (uint16_t)(base & 0xFFFF);
    idt[num].kernel_cs = 0x08;
    idt[num].ist = 0;
    idt[num].attributes = 0x8E; 
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

    pic_remap();
    load_idt(&idtr);
}

void isr_handler(uint64_t* stack_ptr) {
    uint64_t int_no = stack_ptr[15];

    if (int_no < 32) {
        terminal_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
        terminal_clear();
        kprintf("!!! KERNEL PANIC !!!\n");
        kprintf("Exception: %s (%d)\n", exception_messages[int_no], int_no);
        kprintf("Address: %x\n", stack_ptr[17]); 
        while(1) __asm__("hlt");
    } 
    
    if (int_no == 32) {
        timer_handler();
    } else if (int_no == 33) {
        keyboard_handler();
    }

    if (int_no >= 32) {
        if (int_no >= 40) outb(0xA0, 0x20);
        outb(0x20, 0x20);
    }
}

void pic_remap() {
    // Send initialization commands
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    
    // Set vector offsets
    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();
    
    // Tell Master there is a slave
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    
    // Set mode
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    // Enable IRQ0 and IRQ1, mask others
    outb(0x21, 0xFC); 
    outb(0xA1, 0xFF);
}