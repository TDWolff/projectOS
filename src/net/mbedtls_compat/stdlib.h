#ifndef _COMPAT_STDLIB_H
#define _COMPAT_STDLIB_H

#include "../../include/types.h"
#include "../../mem/heap.h"

static inline void* malloc(size_t n)          { return kmalloc((uint64_t)n); }
static inline void* calloc(size_t n, size_t s) {
    size_t total = n * s;
    void* p = kmalloc((uint64_t)total);
    if (p) {
        unsigned char* b = (unsigned char*)p;
        for (size_t i = 0; i < total; i++) b[i] = 0;
    }
    return p;
}
static inline void* realloc(void* ptr, size_t n) {
    /* mbedTLS rarely calls realloc; simple bump-copy version */
    void* np = kmalloc((uint64_t)n);
    if (np && ptr) {
        /* copy min(old,n) bytes — we don't know old size, copy n */
        unsigned char* d = (unsigned char*)np;
        unsigned char* s = (unsigned char*)ptr;
        for (size_t i = 0; i < n; i++) d[i] = s[i];
        kfree(ptr);
    }
    return np;
}
static inline void free(void* p) { kfree(p); }

/* abort — halt the machine */
static inline __attribute__((noreturn)) void abort(void) {
    for (;;) __asm__ volatile("hlt");
}

/* atoi */
static inline int atoi(const char* s) {
    int n = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { n = n * 10 + (*s++ - '0'); }
    return neg ? -n : n;
}

/* strtol — minimal version used by mbedTLS PEM/bignum parsing */
static inline long strtol(const char* s, char** endptr, int base) {
    long n = 0; int neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') base = 8;
        else base = 10;
    }
    while (1) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        n = n * base + d; s++;
    }
    if (endptr) *endptr = (char*)s;
    return neg ? -n : n;
}

static inline unsigned long strtoul(const char* s, char** ep, int base) {
    return (unsigned long)strtol(s, ep, base);
}

#endif /* _COMPAT_STDLIB_H */
