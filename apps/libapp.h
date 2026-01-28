#ifndef LIBAPP_H
#define LIBAPP_H

// Wrapper for syscall 1 (kprintf)
void sys_kprintf(const char* str);
void sys_exit(void);

#endif