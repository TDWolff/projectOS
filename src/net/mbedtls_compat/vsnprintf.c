/* Minimal vsnprintf for mbedTLS error-string formatting.
   Only handles %d, %i, %u, %x, %X, %s, %c, %% with no width/precision. */

#include "stdio.h"
#include "stdint.h"
#include "../../include/types.h"
#include <stdarg.h>

static void _out(char* buf, size_t size, size_t* pos, char c) {
    if (*pos + 1 < size) buf[*pos] = c;
    (*pos)++;
}

static void _outs(char* buf, size_t size, size_t* pos, const char* s) {
    while (*s) _out(buf, size, pos, *s++);
}

static void _outu(char* buf, size_t size, size_t* pos,
                  unsigned long long v, int base, int upper) {
    char tmp[24]; int n = 0;
    if (v == 0) { _out(buf, size, pos, '0'); return; }
    while (v) {
        int d = (int)(v % (unsigned)base);
        tmp[n++] = (d < 10) ? ('0' + d) : (upper ? 'A' : 'a') + d - 10;
        v /= (unsigned)base;
    }
    while (n--) _out(buf, size, pos, tmp[n+1-1+1]); /* reversed */
    /* fix: print in reverse */
    (void)tmp; /* suppress — rewrite properly below */
}

static void _outu2(char* buf, size_t size, size_t* pos,
                   unsigned long long v, int base, int upper) {
    char tmp[24]; int n = 0;
    if (v == 0) { _out(buf, size, pos, '0'); return; }
    while (v) {
        int d = (int)(v % (unsigned long long)base);
        tmp[n++] = (d < 10) ? ('0' + d)
                             : (upper ? 'A' : 'a') + (d - 10);
        v /= (unsigned long long)base;
    }
    /* tmp is reversed */
    for (int i = n - 1; i >= 0; i--)
        _out(buf, size, pos, tmp[i]);
}

int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    size_t pos = 0;
    if (!buf || size == 0) return 0;

    for (; *fmt; fmt++) {
        if (*fmt != '%') { _out(buf, size, &pos, *fmt); continue; }
        fmt++;
        int is_long = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; }
        if (*fmt == 'l') { is_long = 2; fmt++; }

        switch (*fmt) {
        case '%': _out(buf, size, &pos, '%'); break;
        case 'c': _out(buf, size, &pos, (char)va_arg(ap, int)); break;
        case 's': _outs(buf, size, &pos, va_arg(ap, const char*)); break;
        case 'd': case 'i': {
            long long v = (is_long == 2) ? va_arg(ap, long long)
                        : (is_long == 1) ? (long long)va_arg(ap, long)
                                         : (long long)va_arg(ap, int);
            if (v < 0) { _out(buf, size, &pos, '-'); v = -v; }
            _outu2(buf, size, &pos, (unsigned long long)v, 10, 0);
            break;
        }
        case 'u': {
            unsigned long long v = (is_long == 2) ? va_arg(ap, unsigned long long)
                                 : (is_long == 1) ? (unsigned long long)va_arg(ap, unsigned long)
                                                  : (unsigned long long)va_arg(ap, unsigned int);
            _outu2(buf, size, &pos, v, 10, 0);
            break;
        }
        case 'x': case 'X': {
            unsigned long long v = (is_long == 2) ? va_arg(ap, unsigned long long)
                                 : (is_long == 1) ? (unsigned long long)va_arg(ap, unsigned long)
                                                  : (unsigned long long)va_arg(ap, unsigned int);
            _outu2(buf, size, &pos, v, 16, *fmt == 'X');
            break;
        }
        case 'p': {
            unsigned long long v = (unsigned long long)(uintptr_t)va_arg(ap, void*);
            _outs(buf, size, &pos, "0x");
            _outu2(buf, size, &pos, v, 16, 0);
            break;
        }
        default:
            _out(buf, size, &pos, '%');
            _out(buf, size, &pos, *fmt);
            break;
        }
    }
    if (pos < size) buf[pos] = '\0';
    else buf[size - 1] = '\0';
    return (int)pos;
}
