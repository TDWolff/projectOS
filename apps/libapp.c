#include "libapp.h"

// We define types manually because we have no stdint
typedef unsigned long long uint64_t;

// This function is the bridge from user C code to kernel space
void sys_kprintf(const char* str) {
    __asm__ volatile (
        "int $0x80"
        : 
        : "a"(1), "D"(str)
        : "memory"
    );
}

void sys_get_fb_info(fb_info_t* info) {
    __asm__ volatile (
        "int $0x80"
        : 
        : "a"(5), "D"(info)
        : "memory"
    );
}

void sys_exit() {
    __asm__ volatile (
        "int $0x80"
        : 
        : "a"(60), "D"(0)
        : "memory"
    );
}

char sys_get_key() {
    char c;
    __asm__ volatile (
        "int $0x80"
        : "=a"(c)
        : "a"(10) /* Syscall 10 = Get Key */
        : "memory"
    );
    return c;
}

void sys_list_files(file_t* buffer) {
    __asm__ volatile (
        "int $0x80"
        : 
        : "a"(20), "D"(buffer)
        : "memory"
    );
}

int sys_read_file(const char* filename, char* buffer, int max_size) {
    int bytes_read;
    __asm__ volatile (
        "int $0x80"
        : "=a"(bytes_read)
        : "a"(21), "D"(filename), "S"(buffer), "d"(max_size)
        : "memory"
    );
    return bytes_read;
}