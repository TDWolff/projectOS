#include "drivers/vga.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "drivers/shell.h"
#include "cpu/idt.h"
#include "cpu/task.h"
#include "mem/pmm.h"
#include "mem/heap.h"
#include "lib/stdio.h"

void task_a() {
    while(1) {
        (*(uint16_t*)0xb8f9c) = (uint16_t)'A' | (uint16_t)0x0E00;
        __asm__ volatile("hlt");
    }
}

void task_b() {
    while(1) {
        (*(uint16_t*)0xb8f9e) = (uint16_t)'B' | (uint16_t)0x0D00;
        __asm__ volatile("hlt");
    }
}

void kernel_main(void* mb_info) {
    terminal_initialize();
    pmm_init(mb_info);
    heap_init();
    
    task_init();          
    create_task(task_a);  
    create_task(task_b);  

    idt_init();           
    timer_init(100);
    keyboard_init();
    
    shell_init();

    while(1) { __asm__ volatile ("hlt"); }
}