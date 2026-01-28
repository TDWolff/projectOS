#include "shell.h"
#include "vga.h"
#include "timer.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../mem/heap.h"
#include "../cpu/idt.h"
#include "../fs/initrd.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "mouse.h"

// Shell Window Coordinates
#define SHELL_X 200
#define SHELL_Y 150
#define SHELL_WIN_W 600
#define SHELL_WIN_H 400

#define MAX_COMMAND_LEN 128
static char command_buffer[MAX_COMMAND_LEN];
static int buffer_idx = 0;

static bool shell_visible = false;
static bool last_mouse_button = false;

void shell_init() {
    memset(command_buffer, 0, MAX_COMMAND_LEN);
    buffer_idx = 0;
    shell_visible = false;
    
    // Set terminal background to teal so text doesn't have black boxes
    terminal_set_bg(0x008080);
    video_draw_desktop();
}

void draw_shell_window() {
    if (!shell_visible) return;

    int x = SHELL_X;
    int y = SHELL_Y;
    int w = SHELL_WIN_W;
    int h = SHELL_WIN_H;

    // Window shadow
    draw_rect(x + 4, y + 4, w, h, 0x404040);
    // Window Body
    draw_rect(x, y, w, h, 0xC0C0C0);
    // Title Bar
    draw_rect(x + 2, y + 2, w - 4, 25, 0x000080); // Classic Blue title
    
    // Label for title bar
    video_draw_text(x + 10, y + 5, "Terminal", 0xFFFFFF);

    // Text Area
    draw_rect(x + 5, y + 30, w - 10, h - 35, 0x000000); // Black terminal area
}

void shell_set_visible(bool visible) {
    shell_visible = visible;
    if (visible) {
        // Redraw desktop and window to ensure clean state
        video_draw_desktop();
        draw_shell_window(); 
        
        // When opening the shell, set the kernel's text cursor inside the black box
        video_set_cursor(SHELL_X + 10, SHELL_Y + 35);
        video_set_color(0xFFFFFFFF, 0x000000); // White text, Black bg
        
        // Remove the leading newline so it starts at the top-left of the black box
        kprintf("root %% ");
    } else {
        // When closing, reset everything
        video_draw_desktop();
        video_set_color(0xFFFFFFFF, 0x008080); // White text, Teal bg
    }
}

void shell_check_click() {
    int mx = mouse_get_x();
    int my = mouse_get_y();
    bool clicked = mouse_get_buttons() & 0x01; // Left click

    // Check "Start" button (5, height-35, 80, 30)
    int btn_x = 5;
    int btn_y = get_fb_height() - 35;
    
    if (clicked && !last_mouse_button) {
        if (mx >= btn_x && mx <= btn_x + 80 && my >= btn_y && my <= btn_y + 30) {
            // Toggle visibility
            shell_set_visible(!shell_visible);
        }
    }
    last_mouse_button = clicked;
}

void execute_command(char* input) {
    if (!shell_visible) return;
    // 1. Help
    if (strcmp(input, "help") == 0) {
        kprintf("\nls, cat, clear, ticks, divzero, echo, run <program>");
    } 
    // 1. RUN (Execute Program) - Quick hack parsing
    else if (input[0] == 'r' && input[1] == 'u' && input[2] == 'n' && input[3] == ' ') {
        char* filename = input + 4;
        file_t* f = initrd_open(filename);

        if (f) {
            kprintf("\nLoading program '%s'...\n", filename);
            
            // 1. Create a new address space for the app
            extern uint64_t p4_table[]; // Access kernel table
            uint64_t* app_pagemap = vmm_create_address_space();

            // 2. Allocate and map the app to 4GB (User Space Territory)
            // We'll calculate how many pages we need
            uint64_t num_pages = (f->size + PAGE_SIZE - 1) / PAGE_SIZE;
            uint64_t app_virtual_base = 0x100000000;

            for (uint64_t i = 0; i < num_pages; i++) {
                void* physical_page = pmm_alloc();
                // Map the app virtual address to the allocated physical page
                // Crucial: Use PAGE_USER flag so the app can access its own memory!
                vmm_map_page(app_pagemap, app_virtual_base + (i * PAGE_SIZE), (uint64_t)physical_page, PAGE_WRITABLE | PAGE_USER);
                
                // Copy data to the physical page
                uint64_t copy_size = (i == num_pages - 1) ? (f->size % PAGE_SIZE) : PAGE_SIZE;
                if (copy_size == 0) copy_size = PAGE_SIZE; // Handle exact multiples
                memcpy(physical_page, (void*)(f->address + (i * PAGE_SIZE)), copy_size);
            }

            // 3. Switch to the new page map and jump!
            uint64_t kernel_pagemap;
            __asm__ volatile("mov %%cr3, %0" : "=r"(kernel_pagemap));

            vmm_switch_pagemap(app_pagemap);

            void (*app_entry)(void) = (void*)app_virtual_base;
            app_entry();
            
            // 4. Return to kernel address space
            vmm_switch_pagemap((uint64_t*)kernel_pagemap);

            // 5. Force a full screen redraw to clear the app's mess
            video_draw_desktop();
            shell_visible = true; // Ensure shell is visible after app return
            draw_shell_window();
            kprintf("\nProgram finished.");
        } else {
            kprintf("\nProgram not found: %s", filename);
        }
    }
    // 2. LS (List Files)
    else if (strcmp(input, "ls") == 0) {
        file_t* files = initrd_get_files();
        kprintf("\n--- Filesystem ---\n");
        for(int i=0; i<MAX_FILES; i++) {
            if(files[i].exists) {
                kprintf("%s  (%d bytes)\n", files[i].name, files[i].size);
            }
        }
    }
    // 3. CAT (Read File) - Quick hack parsing
    else if (input[0] == 'c' && input[1] == 'a' && input[2] == 't' && input[3] == ' ') {
        char* filename = input + 4; // Skip "cat "
        file_t* f = initrd_open(filename);
        
        if (f) {
            kprintf("\n");
            char* content = (char*)f->address;
            for(uint64_t i=0; i < f->size; i++) {
                kprint_char(content[i]);
            }
        } else {
            kprintf("\nFile not found: %s", filename);
        }
    }
    else if (strcmp(input, "clear") == 0) {
        video_draw_desktop();
        draw_shell_window();
        video_set_cursor(SHELL_X + 10, SHELL_Y + 35);
        kprintf("root %% ");
        return;
    } 
    else if (strcmp(input, "ticks") == 0) {
        kprintf("\nSystem ticks: %d", get_ticks());
    }
    else if (strcmp(input, "divzero") == 0) {
        kprintf("\nDividing by zero...");
        volatile int a = 1;
        volatile int b = 0;
        volatile int c = a / b;
        (void)c;
    }
    else if (input[0] == 'e' && input[1] == 'c' && input[2] == 'h' && input[3] == 'o') {
        kprintf("\n%s", input + 5); 
    }
    else if (strlen(input) > 0) {
        kprintf("\nUnknown: %s", input);
    }
    kprintf("\nroot %% "); // Print prompt with newline for next line
}

void shell_update(char c) {
    shell_check_click();
    
    if (!shell_visible) return;

    if (c == '\n') {
        command_buffer[buffer_idx] = '\0';
        execute_command(command_buffer);
        memset(command_buffer, 0, MAX_COMMAND_LEN);
        buffer_idx = 0;
    } else if (c == '\b') {
        if (buffer_idx > 0) {
            buffer_idx--;
            command_buffer[buffer_idx] = 0;
            kprint_char('\b');
        }
    } else {
        if (buffer_idx < MAX_COMMAND_LEN - 1) {
            command_buffer[buffer_idx++] = c;
            kprint_char(c);
        }
    }
}