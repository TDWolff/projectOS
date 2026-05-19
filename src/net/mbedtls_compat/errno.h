#ifndef _COMPAT_ERRNO_H
#define _COMPAT_ERRNO_H

/* Enough errno values for mbedTLS error paths */
static int _compat_errno = 0;
#define errno _compat_errno

#define EINVAL  22
#define ENOMEM  12
#define ENOENT   2
#define EIO      5
#define EPERM    1
#define EAGAIN  11
#define ERANGE  34
#define ENOSYS  38

#endif /* _COMPAT_ERRNO_H */
