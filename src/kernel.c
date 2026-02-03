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
#include "drivers/systemui.h" 
#include "lib/float.h"
#include "lib/settings.h" // Added settings

void kernel_main(void* mb_info) {
    terminal_initialize();
    enable_fpu(); // Enable Floating Point Unit
    pmm_init(mb_info);
    heap_init();
    initrd_init(mb_info); // Initialize file system (ramdisk)
    settings_init();      // Initialize settings (needs initrd)
    video_init(mb_info);
    
    idt_init(); 
    timer_init(100);
    keyboard_init();
    
    // Multi-tasking demo suspended for clean UI
    // task_init();
    // create_task(task_a);
    // create_task(task_b);
    shell_init();
    // taskbar_init(); // Initialize and draw the taskbar
    systemui_init(); // Initialize the new System UI (Top bar + Dock)
    mouse_init(); // Moved below shell_init

    uint64_t last_tick = 0;

    while(1) { 
        shell_check_click(); // Keep checking for mouse clicks on the taskbar
        
        // Refresh the screen from double buffer
        video_swap();
        
        // Update System UI (Clock) every second (approx 100 ticks)
        if (get_ticks() - last_tick >= 100) {
             systemui_update();
             last_tick = get_ticks();
        }
    }
}