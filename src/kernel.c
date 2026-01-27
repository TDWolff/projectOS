#include "drivers/vga.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "drivers/shell.h"
#include "lib/stdio.h"
#include "cpu/idt.h"
#include "mem/pmm.h"
#include "mem/heap.h"

void kernel_main(void* mb_info) {
    // 1. Initialize Video
    terminal_initialize();
    
    // 2. Setup Memory (Crucial for shell and heap)
    pmm_init(mb_info);
    heap_init();
    
    // 3. Setup Shell logic
    shell_init();

    // 4. Setup Interrupts LAST
    // This order ensures the keyboard has a working shell/heap to talk to
    idt_init(); 
    timer_init(100);
    keyboard_init();

    kprintf("\nProjectOS 64-bit Kernel Initialized.\n");
    kprintf("Interrupts are now live.\n> ");

    while(1) {
        // Wait for an interrupt (keyboard or timer)
        __asm__ volatile ("hlt");
    }
}