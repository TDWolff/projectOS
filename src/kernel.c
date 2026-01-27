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

void task_a() {
    while(1) {
        // Draw a pulse bar
        static int x = 0;
        draw_rect(10, 740, 100, 5, 0x222222); // Background
        draw_rect(10, 740, x, 5, 0xFFFF00);   // Yellow progress
        x++; if(x > 100) x = 0;
        sleep(2);
    }
}

void task_b() {
    while(1) {
        // Draw a pulse bar
        static int x = 0;
        draw_rect(120, 740, 100, 5, 0x222222); // Background
        draw_rect(120, 740, x, 5, 0xFF00FF);   // Pink progress
        x++; if(x > 100) x = 0;
        sleep(5);
    }
}

void kernel_main(void* mb_info) {
    terminal_initialize();
    pmm_init(mb_info);
    heap_init();
    initrd_init(mb_info);
    video_init(mb_info);
    
    idt_init(); 
    timer_init(100);
    keyboard_init();
    mouse_init(); // <--- INITIALIZE MOUSE
    
    task_init();
    create_task(task_a);
    create_task(task_b);
    shell_init();

    while(1) { __asm__ volatile ("hlt"); }
}