#ifndef LIBAPP_H
#define LIBAPP_H

typedef unsigned long long uint64_t;
typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;

typedef struct {
    uint64_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
} fb_info_t;

// Wrapper for syscall 1 (kprintf)
void sys_kprintf(const char* str);
void sys_get_fb_info(fb_info_t* info);
void sys_exit(void);

// New: Syscall for Keyboard
char sys_get_key(void);

#endif