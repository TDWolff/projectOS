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
#include "drivers/window.h" // Added window manager
#include "drivers/terminal_window.h"
#include "lib/float.h"
#include "lib/settings.h" // Added settings
#include "drivers/compositor.h"
#include "drivers/dock.h"

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
    mouse_init();
    
    // Initialize System UI (Top Bar, Dock)
    systemui_init();

    // Initialize shell without opening a terminal window by default.
    // The dock launcher can create/focus a terminal window and attach the shell output sink.
    shell_init();
    shell_set_output_sink(0, 0);

    uint64_t last_tick = 0;

    while(1) { 
        shell_check_click(); // Keep checking for mouse clicks on the taskbar
        
        // Handle Window Input (Dragging)
        window_handle_mouse(mouse_get_x(), mouse_get_y(), mouse_get_buttons());

    // Dock hover/click + draw icons
    dock_update(mouse_get_x(), mouse_get_y(), (mouse_get_buttons() & 1) != 0);
        
        // Refresh the screen from double buffer
        compositor_swap_buffers();
        
        // Update System UI (Clock) every second (approx 100 ticks)
        if (get_ticks() - last_tick >= 100) {
             systemui_update();
             last_tick = get_ticks();
        }
    }
}