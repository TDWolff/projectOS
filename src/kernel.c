#include "drivers/vga.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "drivers/shell.h"
#include "lib/stdio.h"
#include "cpu/idt.h"
#include "mem/pmm.h"
#include "mem/heap.h"
#include "fs/initrd.h"
#include "cpu/task.h"
#include "drivers/mouse.h"
#include "drivers/bmp.h"

void kernel_main(void* mb_info) {
    terminal_initialize();
    pmm_init(mb_info);
    heap_init();
    initrd_init(mb_info);
    video_init(mb_info);
    
    idt_init(); 
    timer_init(100);
    keyboard_init();
    
    // Multi-tasking demo suspended for clean UI
    // task_init();
    // create_task(task_a);
    // create_task(task_b);
    shell_init();
    mouse_init(); // Moved below shell_init

    while(1) { 
        shell_check_click(); // Keep checking for mouse clicks on the taskbar
        
        // Refresh the screen from double buffer
        video_swap();
        
        // Removed hlt so we refresh comfortably, or use a timer interrupt to drive this
        // __asm__ volatile ("hlt"); 
    }
}