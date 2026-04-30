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

// Networking (Polaris-inspired)
#include "net/net.h"
#include "drivers/net/e1000.h"

// Persistent storage scaffolding (Phase 0/1)
#include "fs/pfs/pfs.h"

void kernel_main(void* mb_info) {
    terminal_initialize();
    enable_fpu(); // Enable Floating Point Unit
    pmm_init(mb_info);
    heap_init();
    initrd_init(mb_info); // Initialize file system (ramdisk)

    // Initialize persistent storage (disk-backed) *before* settings.
    // This allows /user/settings.pset to override initrd settings.pset.
    pfs_init();

    settings_init();      // Initialize settings (initrd fallback, /user preferred)
    video_init(mb_info);

    // NOTE: Disabled: the smoketest writes to a fixed LBA and can corrupt the
    // on-disk filesystem if it overlaps the FAT32 partition start.
    // Re-enable only after moving it to a safe reserved region.
    // (void)pfs_persist_smoketest();
    
    idt_init(); 
    timer_init(100);
    keyboard_init();
    mouse_init();

    // Networking core + loopback device.
    // Real NIC drivers can register a net_nic_interfaces_t and call net_handle_packet().
    net_init();
    
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
             e1000_poll();
             last_tick = get_ticks();
        }
    }
}