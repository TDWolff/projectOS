#include "libapp.h"

// Redundant types removed as they are in libapp.h
// typedef unsigned long long uint64_t;
// typedef unsigned int uint32_t;
// typedef unsigned short uint16_t;
// typedef unsigned char uint8_t;
typedef _Bool bool;
#define true 1
#define false 0

// Forward declaration
void _start() __attribute__((section(".text.entry")));

// Standard string functions
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strlen(const char* str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

// Shell Variables
#define MAX_COMMAND_LEN 128
char command_buffer[MAX_COMMAND_LEN];
int buffer_idx = 0;

void kprint(const char* str) {
    sys_kprintf(str);
}

void kprint_char(char c) {
    char str[2] = {c, '\0'};
    sys_kprintf(str);
}

void execute_command(char* input) {
    if (strcmp(input, "help") == 0) {
        kprint("\nAvailable commands: help, exit, echo <text>, version");
    }
    else if (strcmp(input, "exit") == 0) {
        sys_exit();
    }
    else if (strcmp(input, "version") == 0) {
        kprint("\nProjectOS Terminal v1.0 (User Space Edition)");
    }
    else if (input[0] == 'e' && input[1] == 'c' && input[2] == 'h' && input[3] == 'o') {
         kprint("\n");
         kprint(input + 5);
    }
    else {
        kprint("\nUnknown command: ");
        kprint(input);
    }
    kprint("\nuser % ");
}

void _start() {
    sys_kprintf("Terminal Started.\n");
    
    // Test infinite loop without syscalls first
    // while(1) {}

    kprint("Welcome to ProjectOS User Terminal!\n");
    kprint("Type 'help' for commands.\n");
    kprint("user % ");

    // Main Loop
    while (1) {
        char c = sys_get_key();
        if (c == 0) continue; // Non-blocking spin wait

        if (c == '\n') {
            kprint_char('\n');
            if (buffer_idx > 0) {
                execute_command(command_buffer);
            } else {
                kprint("user % ");
            }
            
            // Clear buffer
            for (int i = 0; i < MAX_COMMAND_LEN; i++) command_buffer[i] = 0;
            buffer_idx = 0;
        } 
        else if (c == '\b') {
            if (buffer_idx > 0) {
                buffer_idx--;
                command_buffer[buffer_idx] = 0;
                kprint_char('\b');
            }
        }
        else {
            if (buffer_idx < MAX_COMMAND_LEN - 1) {
                command_buffer[buffer_idx++] = c;
                command_buffer[buffer_idx] = '\0';
                kprint_char(c);
            }
        }
    }

    sys_exit();
}
