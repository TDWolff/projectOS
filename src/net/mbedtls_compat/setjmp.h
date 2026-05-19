#ifndef _COMPAT_SETJMP_H
#define _COMPAT_SETJMP_H

/* mbedTLS doesn't use setjmp directly but some included paths pull this in */
typedef unsigned long jmp_buf[8];

static inline int setjmp(jmp_buf e)              { (void)e; return 0; }
static inline __attribute__((noreturn)) void longjmp(jmp_buf e, int v) {
    (void)e; (void)v;
    for (;;) __asm__ volatile("hlt");
}

#endif /* _COMPAT_SETJMP_H */
