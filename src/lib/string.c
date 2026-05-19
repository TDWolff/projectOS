#include "string.h"

size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}

void* memcpy(void* dest, const void* src, size_t n) {
    // Optimization: Copy 8 bytes (64 bits) at a time if possible
    uint64_t* d64 = (uint64_t*)dest;
    const uint64_t* s64 = (const uint64_t*)src;
    
    // Check if pointers are 8-byte aligned and n is large enough
    if (n >= 8 && ((uint64_t)dest & 7) == 0 && ((uint64_t)src & 7) == 0) {
        size_t n64 = n / 8;
        for (size_t i = 0; i < n64; i++) {
            d64[i] = s64[i];
        }
        
        // Adjust for remaining bytes
        size_t offset = n64 * 8;
        dest = (char*)dest + offset;
        src = (const char*)src + offset;
        n -= offset;
    }

    // Copy remaining bytes (or all bytes if not aligned)
    char* d = (char*)dest;
    const char* s = (const char*)src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
    return dest;
}

void* memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    for (size_t i = 0; i < n; i++)
        p[i] = (unsigned char)c;
    return s;
}

void reverse(char* s) {
    int i, j;
    char c;
    for (i = 0, j = strlen(s)-1; i < j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* p1 = (const uint8_t*)a;
    const uint8_t* p2 = (const uint8_t*)b;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return (int)p1[i] - (int)p2[i];
        }
    }
    return 0;
}