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

// Simple itoa for numbers (needed for LS)
void itoa(unsigned long long n, char* str) {
    if (n == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }
    int i = 0;
    unsigned long long temp = n;
    while (temp != 0) {
        i++;
        temp /= 10;
    }
    str[i] = '\0';
    while (n != 0) {
        str[--i] = (n % 10) + '0';
        n /= 10;
    }
}

void execute_command(char* input) {
    if (strcmp(input, "help") == 0) {
        kprint("\nAvailable commands: help, exit, echo <text>, version, ls, cat <file>");
    }
    else if (strcmp(input, "exit") == 0) {
        sys_exit();
    }
    else if (strcmp(input, "ls") == 0) {
         file_t files[MAX_FILES];
         sys_list_files(files);
         kprint("\n--- User Space Filesystem ---\n");
         for (int i = 0; i < MAX_FILES; i++) {
             if (files[i].exists) {
                 kprint(files[i].name);
                 kprint(" (");
                 char size_buf[32];
                 itoa(files[i].size, size_buf);
                 kprint(size_buf);
                 kprint(" bytes)\n");
             }
         }
    }
    else if (input[0] == 'c' && input[1] == 'a' && input[2] == 't' && input[3] == ' ') {
         char* filename = input + 4; // Skip "cat "
         // Should allocate larger buffer for big files, but stack is safe enough for small ones
         char buffer[1024]; 
         int bytes = sys_read_file(filename, buffer, 1023);
         
         if (bytes >= 0) {
             buffer[bytes] = '\0'; // Null terminate
             kprint("\n");
             kprint(buffer);
             kprint("\n");
         } else {
             kprint("\nFile not found: ");
             kprint(filename);
         }
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
