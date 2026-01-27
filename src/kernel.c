#include "drivers/vga.h"
#include "drivers/timer.h"
#include "drivers/shell.h" // Added
#include "lib/stdio.h"
#include "cpu/idt.h"
#include "mem/pmm.h"
#include "mem/heap.h"

void kernel_main(void* mb_info) {
    terminal_initialize();
    idt_init();
    timer_init(100);

    pmm_init(mb_info);
    heap_init();

    // Launch the interactive shell
    shell_init();

    while(1) {
        __asm__ volatile ("hlt");
    }
}