#ifndef _COMPAT_STDIO_H
#define _COMPAT_STDIO_H

#include "../../include/types.h"
#include <stdarg.h>

/* mbedTLS only uses snprintf/vsnprintf for error formatting */

int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap);

static inline int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

/* FILE* and friends — mbedTLS uses these only when MBEDTLS_FS_IO is set,
   which we #undef in our config overrides.  Define bare stubs to satisfy
   headers that are included unconditionally. */
typedef struct { int dummy; } FILE;
#ifndef NULL
#define NULL ((void*)0)
#endif
#define EOF  (-1)
static inline int fprintf(FILE* f, const char* fmt, ...) { (void)f; (void)fmt; return 0; }
static inline int fflush(FILE* f)                         { (void)f; return 0; }

/* printf — used by mbedtls_printf (maps to printf when PLATFORM_PRINTF_ALT not set).
   We silently drop all mbedTLS debug/diagnostic output. */
static inline int printf(const char* fmt, ...) { (void)fmt; return 0; }

#endif /* _COMPAT_STDIO_H */
