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

static int shell_x = SHELL_X;
static int shell_y = SHELL_Y;

#define MAX_COMMAND_LEN 128
static char command_buffer[MAX_COMMAND_LEN];
static int buffer_idx = 0;

static bool shell_visible = false;
static bool last_mouse_button = false;
static bool is_app_running = false;

// Forward Declaration
void run_program(const char* filename);

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

    // The kernel no longer draws the window body, as the user app 'terminal.pexe' handles it.
    // However, we still might need to enforce the visual area if the shell is toggled.
    // For now, we stub this out so we don't end up with double-drawing or kernel overwriting user gfx.
}

void shell_set_visible(bool visible) {
    shell_visible = visible;
    if (visible) {
        // Redraw desktop and window to ensure clean state
        video_draw_desktop();
        draw_shell_window(); 
        
        // When opening the shell, set the kernel's text cursor inside the black box
        video_set_cursor(shell_x + 10, shell_y + 35);
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
    
    if (clicked) {
        if (!last_mouse_button) {
            // Mouse Down Event
            if (mx >= btn_x && mx <= btn_x + 80 && my >= btn_y && my <= btn_y + 30) {
                shell_visible = true;
                kprintf("\nAttempting to run terminal.pexe...\n");
                run_program("terminal.pexe");
            }
        }
    }
    last_mouse_button = clicked;
}

void run_program(const char* filename) {
    file_t* f = initrd_open(filename);

    if (f) {
        // Handle nested execution: if an app is already running (e.g. terminal.pexe),
        // we want to restore that state when the child process (e.g. stress_test.pexe) finishes.
        bool parent_app_running = is_app_running;
        is_app_running = true;

        // Redraw desktop and window to ensure clean state
        video_draw_desktop();
        draw_shell_window();
        video_set_cursor(shell_x + 10, shell_y + 35);
        video_set_color(0xFFFFFFFF, 0x000000); // White text, Black bg

        // Optional: show a very brief status message. Commented out so
        // the user-space terminal owns the entire area and can draw its
        // own welcome text and prompt without leftover kernel text.
        // kprintf("Loading program '%s'...\n", filename);
        
        // 1. Create a new address space for the app
        extern uint64_t p4_table[]; // Access kernel table
        uint64_t* app_pagemap = vmm_create_address_space();

        // 2. Allocate and map the app to 4GB (User Space Territory)
        // We'll calculate how many pages we need, plus extra for .bss (globals) and stack
        uint64_t file_pages = (f->size + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t total_pages = file_pages + 64; // Add 256KB extra for BSS/Stack growth
        uint64_t app_virtual_base = 0x100000000;

        for (uint64_t i = 0; i < total_pages; i++) {
            void* physical_page = pmm_alloc();
            memset(physical_page, 0, PAGE_SIZE); // Ensure clean zero-init

            // Map the app virtual address to the allocated physical page
            // Crucial: Use PAGE_USER flag so the app can access its own memory!
            vmm_map_page(app_pagemap, app_virtual_base + (i * PAGE_SIZE), (uint64_t)physical_page, PAGE_WRITABLE | PAGE_USER);
            
            // Copy data to the physical page only if within file bounds
            if (i < file_pages) {
                uint64_t offset = i * PAGE_SIZE;
                uint64_t remaining = f->size - offset;
                uint64_t copy_size = (remaining > PAGE_SIZE) ? PAGE_SIZE : remaining;
                
                memcpy(physical_page, (void*)(f->address + offset), copy_size);
            }
        }

        // 3. Switch to the new page map and jump!
        uint64_t kernel_pagemap;
        __asm__ volatile("mov %%cr3, %0" : "=r"(kernel_pagemap));

        vmm_switch_pagemap(app_pagemap);

        void (*app_entry)(void) = (void*)app_virtual_base;
        
        // Debug print to confirm launch
        // kprintf("Jumping to entry point at 0x%x\n", app_virtual_base);
        
        app_entry();
        
    // 4. Return to kernel address space
    vmm_switch_pagemap((uint64_t*)kernel_pagemap);

    // Restore the running state of the parent (e.g. true if returning to terminal.pexe)
    is_app_running = parent_app_running;

    // 5. Redraw the desktop to remove any artifacts left by the program (like stress_test)
    video_draw_desktop();
    shell_visible = true; 
    draw_shell_window();
    video_set_color(0xFFFFFFFF, 0x000000); // Ensure text color is reset for the terminal
    
    } else {
        kprintf("\nProgram not found: %s\n", filename);
        // shell_visible = false; // Reset if failed (removed to keep terminal open)
    }
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
        run_program(filename);
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
        video_set_cursor(shell_x + 10, shell_y + 35);
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
    // Do not process kernel shell commands if an app is running
    // The app will read the keystrokes via syscalls instead.
    if (is_app_running) return;

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