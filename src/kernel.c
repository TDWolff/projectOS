#include "drivers/vga.h"
#include "drivers/timer.h"
#include "lib/stdio.h"
#include "cpu/idt.h"

void kernel_main() {
    terminal_initialize();
    
    // 1. Setup IDT first
    idt_init();
    
    // 2. Setup Timer
    timer_init(100);

    terminal_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintf("ProjectOS System Initializing...\n");
    
    // Sleep countdown
    for(int i = 3; i > 0; i--) {
        kprintf("Ready in %d...\n", i);
        sleep(100);
    }

    terminal_clear();
    kprintf("ProjectOS 64-bit Kernel Console\n");
    kprintf("Type anything to test the keyboard:\n> ");

    // Main loop
    while(1) {
        // 'hlt' stops the CPU until the NEXT interrupt fires.
        // This is much better than a busy loop!
        __asm__ volatile ("hlt");
    }
}