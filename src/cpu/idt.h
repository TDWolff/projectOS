#ifndef IDT_H
#define IDT_H

#include "../include/types.h"

// The IDT entry structure for x86_64
typedef struct {
    uint16_t isr_low;      // Lower 16 bits of ISR address
    uint16_t kernel_cs;    // Kernel code segment (usually 0x08)
    uint8_t  ist;          // Interrupt Stack Table offset (usually 0)
    uint8_t  attributes;   // Type and attributes (0x8E for interrupt gate)
    uint16_t isr_mid;      // Middle 16 bits of ISR address
    uint32_t isr_high;     // Upper 32 bits of ISR address
    uint32_t reserved;     // Set to 0
} __attribute__((packed)) idt_entry_t;

// The IDTR structure (what we pass to the 'lidt' instruction)
typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtr_t;

void idt_init();

#endif