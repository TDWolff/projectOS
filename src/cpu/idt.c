#include "idt.h"
#include "../lib/stdio.h"
#include "../include/ports.h"

void pic_remap(); 

__attribute__((aligned(0x10)))
static idt_entry_t idt[256];
static idtr_t idtr;

extern void isr0();
extern void isr8();
extern void isr13();
extern void isr14();
extern void isr32(); // Timer
extern void isr33(); // Keyboard
extern void load_idt(idtr_t*);
extern void keyboard_handler();

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

    // Hook Exceptions
    idt_set_gate(0, (uint64_t)isr0);
    idt_set_gate(8, (uint64_t)isr8);
    idt_set_gate(13, (uint64_t)isr13);
    idt_set_gate(14, (uint64_t)isr14);
    
    // Hook Hardware Interrupts
    idt_set_gate(32, (uint64_t)isr32);
    idt_set_gate(33, (uint64_t)isr33);

    pic_remap();
    load_idt(&idtr);
    kprintf("IDT/PIC Initialized. Interrupts Enabled.\n");
}

void isr_handler(uint64_t* stack_ptr) {
    uint64_t int_no = stack_ptr[15];
    
    if (int_no == 32) {
        // Timer interrupt - just acknowledge it and move on
        outb(0x20, 0x20);
    } else if (int_no == 33) {
        keyboard_handler();
    } else {
        kprintf("\n--- EXCEPTION %d ---\n", int_no);
        // If it's a GPF (13), print the error code which is at stack_ptr[16]
        if (int_no == 13) kprintf("GPF Error Code: %x\n", stack_ptr[16]);
        while(1);
    }
}

void pic_remap() {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    // 0xFC is 11111100b (Lower two bits 0 = enabled)
    outb(0x21, 0xFC); 
    outb(0xA1, 0xFF); // Mask everything on Slave PIC
}