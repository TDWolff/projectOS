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
    uint64_t font_addr;
    uint32_t font_width;
    uint32_t font_height;
    uint32_t font_bytes;
} fb_info_t;

// Duplicated from initrd.h
#define MAX_FILES 10
typedef struct {
    char name[32];
    uint64_t address;
    uint64_t size;
    uint8_t exists;
} file_t;

// Wrapper for syscall 1 (kprintf)
void sys_kprintf(const char* str);
void sys_get_fb_info(fb_info_t* info);
void sys_exit(void);

// New: Syscall for Keyboard
char sys_get_key(void);

// New: File System Syscalls
void sys_list_files(file_t* buffer);
int sys_read_file(const char* filename, char* buffer, int max_size);
void sys_run(const char* filename);

// New: Syscall for Mouse and Window Management
void sys_get_mouse(int* x, int* y, int* buttons);
void sys_update_window(int x, int y, int w, int h);
void sys_mouse_show();
void sys_mouse_hide();

#endif