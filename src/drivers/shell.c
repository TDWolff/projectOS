#include "shell.h"
#include "vga.h"
#include "timer.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../mem/heap.h"
#include "../cpu/idt.h"

#define MAX_COMMAND_LEN 128
static char command_buffer[MAX_COMMAND_LEN];
static int buffer_idx = 0;

void shell_init() {
    memset(command_buffer, 0, MAX_COMMAND_LEN);
    buffer_idx = 0;
    kprintf("\nProjectOS Shell v1.0\n> ");
}

void execute_command(char* input) {
    if (strcmp(input, "help") == 0) {
        kprintf("\nhelp, clear, ticks, panic, divzero, echo");
    } 
    else if (strcmp(input, "clear") == 0) {
        terminal_clear();
        kprintf("ProjectOS Shell v1.0");
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