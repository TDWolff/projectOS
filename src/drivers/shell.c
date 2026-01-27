#include "shell.h"
#include "vga.h"
#include "timer.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../mem/heap.h"

#define MAX_COMMAND_LEN 128
char* command_buffer;
int buffer_idx = 0;

void shell_init() {
    command_buffer = (char*)kmalloc(MAX_COMMAND_LEN);
    memset(command_buffer, 0, MAX_COMMAND_LEN);
    buffer_idx = 0;
    kprintf("\nProjectOS Shell v1.0\n> ");
}

void execute_command(char* input) {
    if (strcmp(input, "help") == 0) {
        kprintf("\nAvailable commands: help, clear, ticks, panic, echo");
    } 
    else if (strcmp(input, "clear") == 0) {
        terminal_clear();
        kprintf("ProjectOS Shell v1.0");
    } 
    else if (strcmp(input, "ticks") == 0) {
        kprintf("\nSystem ticks: %d", get_ticks());
    }
    else if (strcmp(input, "panic") == 0) {
        int* p = (int*)0;
        int x = 5 / *p; // Force a crash to test your Red Screen!
    }
    else if (input[0] == 'e' && input[1] == 'c' && input[2] == 'h' && input[3] == 'o') {
        kprintf("\n%s", input + 5); // Print everything after "echo "
    }
    else if (strlen(input) > 0) {
        kprintf("\nUnknown command: %s", input);
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
            kprint_char('\b'); // VGA driver handles the visual backspace
        }
    } else {
        if (buffer_idx < MAX_COMMAND_LEN - 1) {
            command_buffer[buffer_idx++] = c;
            kprint_char(c);
        }
    }
}