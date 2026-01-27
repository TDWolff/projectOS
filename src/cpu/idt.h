#ifndef IDT_H
#define IDT_H

#include "../include/types.h"

// This struct matches the order of 'push' instructions in interrupts.asm
typedef struct {
    // Registers pushed by isr_common
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rdx, rcx, rbx, rax, rbp;
    
    // Pushed by the macro/CPU
    uint64_t int_no;
    uint64_t err_code;
    
    // Pushed by CPU automatically
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) registers_t;

typedef struct {
    uint16_t isr_low;
    uint16_t kernel_cs;
    uint8_t  ist;
    uint8_t  attributes;
    uint16_t isr_mid;
    uint32_t isr_high;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtr_t;

void idt_init();
// Allow other files to trigger a manual panic
void kpanic(registers_t* regs, const char* reason);

#endif