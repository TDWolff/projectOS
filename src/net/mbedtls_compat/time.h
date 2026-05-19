#ifndef _COMPAT_TIME_H
#define _COMPAT_TIME_H

#include "../../include/types.h"

/* mbedTLS uses time() only when MBEDTLS_HAVE_TIME is set.
   We #undef both MBEDTLS_HAVE_TIME and MBEDTLS_HAVE_TIME_DATE.
   Provide the type stubs so headers compile. */

typedef uint64_t time_t;
typedef uint64_t clock_t;

struct tm {
    int tm_sec, tm_min, tm_hour;
    int tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};

static inline time_t time(time_t* t) {
    if (t) *t = 0;
    return 0;
}

static inline struct tm* gmtime(const time_t* tp) {
    (void)tp;
    static struct tm z = {0};
    return &z;
}

#endif /* _COMPAT_TIME_H */
