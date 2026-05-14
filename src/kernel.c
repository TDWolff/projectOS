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

#include "cpu/task.h"

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

    // Activate the round-robin scheduler and create the shell worker task.
    // The shell worker runs execute_command so the UI loop below is never blocked.
    task_init();
    create_task(shell_worker_entry);

    uint64_t last_tick  = 0;
    uint64_t last_frame = 0;

    // UI loop: sleep between timer ticks so we run at exactly 100 Hz max.
    // The shell worker task handles commands; this loop is pure UI — never blocks.
    while(1) {
        __asm__ volatile("sti");
        uint64_t now = get_ticks();
        if (now == last_frame) {
            // Nothing to do until the next timer tick — hand CPU back.
            __asm__ volatile("hlt");
            continue;
        }
        last_frame = now;

        shell_check_click();
        window_handle_mouse(mouse_get_x(), mouse_get_y(), mouse_get_buttons());
        dock_update(mouse_get_x(), mouse_get_y(), (mouse_get_buttons() & 1) != 0);
        compositor_swap_buffers();

        if (now - last_tick >= 100) {
            systemui_update();
            e1000_poll();
            last_tick = now;
        }
    }
}