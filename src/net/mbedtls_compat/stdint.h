#ifndef _COMPAT_STDINT_H
#define _COMPAT_STDINT_H

/* Intercept <stdint.h> for mbedTLS files.
   All base types come from types.h (already in the include chain).
   We define the standard include guards so the toolchain's stdint.h
   never runs and cannot redefine them with conflicting underlying types. */

#ifndef TYPES_H
#include "../../include/types.h"
#endif

/* Block the toolchain stdint.h */
#define _STDINT_H
#define _SYS_STDINT_H_
#define __CLANG_STDINT_H
#define _GCC_STDINT_H

/* pointer-sized types */
#ifndef _INTPTR_T
#define _INTPTR_T
typedef int64_t  intptr_t;
#endif
#ifndef _UINTPTR_T
#define _UINTPTR_T
typedef uint64_t uintptr_t;
#endif

typedef int64_t  intmax_t;
typedef uint64_t uintmax_t;

/* Fixed-width limits */
#define INT8_MIN    (-128)
#define INT8_MAX    (127)
#define INT16_MIN   (-32768)
#define INT16_MAX   (32767)
#define INT32_MIN   (-2147483648)
#define INT32_MAX   (2147483647)
#define INT64_MIN   (-9223372036854775807LL - 1)
#define INT64_MAX   (9223372036854775807LL)
#define UINT8_MAX   (255U)
#define UINT16_MAX  (65535U)
#define UINT32_MAX  (4294967295U)
#define UINT64_MAX  (18446744073709551615ULL)
#define SIZE_MAX    UINT64_MAX
#define INTPTR_MIN  INT64_MIN
#define INTPTR_MAX  INT64_MAX
#define UINTPTR_MAX UINT64_MAX
#define INTMAX_MIN  INT64_MIN
#define INTMAX_MAX  INT64_MAX
#define UINTMAX_MAX UINT64_MAX

/* printf format macros used by mbedtls/error.h */
#define PRId32  "d"
#define PRIu32  "u"
#define PRIx32  "x"
#define PRIX32  "X"
#define PRId64  "lld"
#define PRIu64  "llu"
#define PRIx64  "llx"
#define PRIX64  "llX"

/* least/fast types — just alias the exact-width ones */
typedef int8_t   int_least8_t;
typedef int16_t  int_least16_t;
typedef int32_t  int_least32_t;
typedef int64_t  int_least64_t;
typedef uint8_t  uint_least8_t;
typedef uint16_t uint_least16_t;
typedef uint32_t uint_least32_t;
typedef uint64_t uint_least64_t;
typedef int8_t   int_fast8_t;
typedef int16_t  int_fast16_t;
typedef int32_t  int_fast32_t;
typedef int64_t  int_fast64_t;
typedef uint8_t  uint_fast8_t;
typedef uint16_t uint_fast16_t;
typedef uint32_t uint_fast32_t;
typedef uint64_t uint_fast64_t;

#endif /* _COMPAT_STDINT_H */
