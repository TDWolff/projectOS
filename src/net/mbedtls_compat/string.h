#ifndef _COMPAT_STRING_H
#define _COMPAT_STRING_H

#include "../../include/types.h"
#include "../../lib/string.h"

/* memmove — mbedTLS uses this for overlapping copies */
static inline void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i-- > 0;) d[i] = s[i];
    }
    return dst;
}

/* strncmp */
static inline int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if ((unsigned char)a[i] != (unsigned char)b[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

/* strncpy */
static inline char* strncpy(char* dst, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

/* strchr */
static inline char* strchr(const char* s, int c) {
    while (*s) { if (*s == (char)c) return (char*)s; s++; }
    return (c == '\0') ? (char*)s : (void*)0;
}

/* strstr */
static inline char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char* h = haystack, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
    }
    return (void*)0;
}

/* strerror — mbedTLS uses this only for error messages; always return stub */
static inline char* strerror(int e) { (void)e; return "error"; }

/* strlen alias guard */
#ifndef strlen
/* already provided by ../../lib/string.h */
#endif

#endif /* _COMPAT_STRING_H */
