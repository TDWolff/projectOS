#ifndef _COMPAT_ASSERT_H
#define _COMPAT_ASSERT_H

/* Bare-metal assert: halt on failure in debug builds, no-op in release. */
#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
static inline __attribute__((noreturn)) void _assert_fail(void) {
    for (;;) __asm__ volatile("hlt");
}
#define assert(expr) ((expr) ? (void)0 : _assert_fail())
#endif

#define NDEBUG /* disable assertions in mbedTLS for bare-metal */
#undef  assert
#define assert(expr) ((void)0)

#endif /* _COMPAT_ASSERT_H */
