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

void sys_exit() {
    __asm__ volatile (
        "int $0x80"
        : 
        : "a"(60), "D"(0)
        : "memory"
    );
}