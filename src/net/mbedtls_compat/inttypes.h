#ifndef _COMPAT_INTTYPES_H
#define _COMPAT_INTTYPES_H

/* Provide PRIx32 / PRIu32 / PRId64 etc. for mbedTLS error/debug headers. */
#include "stdint.h"

/* Block system inttypes.h */
#define _INTTYPES_H
#define _SYS_INTTYPES_H_

/* PRId / PRIu / PRIx / PRIX for 8-bit */
#define PRId8   "d"
#define PRIi8   "i"
#define PRIu8   "u"
#define PRIo8   "o"
#define PRIx8   "x"
#define PRIX8   "X"
/* 16-bit */
#define PRId16  "d"
#define PRIi16  "i"
#define PRIu16  "u"
#define PRIo16  "o"
#define PRIx16  "x"
#define PRIX16  "X"
/* 32-bit */
#define PRId32  "d"
#define PRIi32  "i"
#define PRIu32  "u"
#define PRIo32  "o"
#define PRIx32  "x"
#define PRIX32  "X"
/* 64-bit */
#define PRId64  "lld"
#define PRIi64  "lli"
#define PRIu64  "llu"
#define PRIo64  "llo"
#define PRIx64  "llx"
#define PRIX64  "llX"
/* max */
#define PRIdMAX "lld"
#define PRIuMAX "llu"
#define PRIxMAX "llx"
/* ptr */
#define PRIdPTR "lld"
#define PRIuPTR "llu"
#define PRIxPTR "llx"

#endif /* _COMPAT_INTTYPES_H */
