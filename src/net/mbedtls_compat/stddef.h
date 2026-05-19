#ifndef _COMPAT_STDDEF_H
#define _COMPAT_STDDEF_H

/* Intercept <stddef.h> — provide the handful of things mbedTLS needs
   without conflicting with our types.h definitions. */

#ifndef TYPES_H
#include "../../include/types.h"
#endif

/* Block the toolchain's stddef.h from loading */
#define _STDDEF_H
#define _STDDEF_H_
#define __STDDEF_H
#define _SYS_STDDEF_H_

/* Block the clang split-header __stddef_size_t.h etc. */
#define __STDDEF_SIZE_T_H
#define __SIZE_T__
#define _SIZE_T
#define __CLANG_STDDEF_SIZE_T_H

/* NULL */
#ifndef NULL
#define NULL ((void*)0)
#endif

/* offsetof */
#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

/* ptrdiff_t */
#ifndef _PTRDIFF_T
#define _PTRDIFF_T
typedef int64_t ptrdiff_t;
#endif

/* max_align_t — rarely needed but some headers reference it */
typedef long double max_align_t;

#endif /* _COMPAT_STDDEF_H */
