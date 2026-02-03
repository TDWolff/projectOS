#ifndef STDIO_H
#define STDIO_H

#include "../include/types.h"

// A simple printf clone
void kprintf(const char* format, ...);

// Simple character printing
void kprint_char(char c);

void itoa(int64_t n, char* str, int base);

#endif