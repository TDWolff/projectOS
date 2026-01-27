#include "shell.h"
#include "vga.h"
#include "timer.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../mem/heap.h"
#include "../cpu/idt.h"
#include "../fs/initrd.h" // Added

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
        kprintf("\nls, cat, clear, ticks, divzero, echo");
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