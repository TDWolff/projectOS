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

#define MAX_COMMAND_LEN 128
static char command_buffer[MAX_COMMAND_LEN];
static int buffer_idx = 0;

void shell_init() {
    memset(command_buffer, 0, MAX_COMMAND_LEN);
    buffer_idx = 0;
    kprintf("\nProjectOS Shell v1.1\n> ");
}

void execute_command(char* input) {
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
            terminal_clear();
            kprintf("\nProgram finished.");
            // In a real OS, we'd free the app_pagemap and its pages here
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
        terminal_clear();
        kprintf("ProjectOS Shell v1.1");
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
    kprintf("\n> ");
}

void shell_update(char c) {
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