#include "drivers/vga.h"
#include "lib/stdio.h"
#include "cpu/idt.h"

void kernel_main() {
    terminal_initialize();
    idt_init(); // This now includes pic_remap and STI

    kprintf("ProjectOS Loaded. Try typing something!\n> ");

    while(1) {
        __asm__("hlt"); // Wait for the next interrupt (saves CPU power)
    }
}