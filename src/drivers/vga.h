#ifndef VGA_H
#define VGA_H
#include "../include/types.h"

void video_init(void* mb_info);
void putpixel(int x, int y, uint32_t color);
uint32_t getpixel(int x, int y);
void draw_rect(int x, int y, int w, int h, uint32_t color);
void terminal_initialize();
void terminal_clear();
void terminal_set_color(uint8_t fg, uint8_t bg);
void kprint(const char* str);
void kprint_char(char c);

void* get_framebuffer_addr();
uint32_t get_fb_pitch();

void video_blit_8x8(int x, int y, uint32_t* data);
uint32_t video_get_pixel(int x, int y);

// Framebuffer Info for User Space
typedef struct {
    uint64_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
} fb_info_t;

// Pass display info to user applications
void video_get_info(fb_info_t* info);

#define VGA_COLOR_BLACK 0
#define VGA_COLOR_BLUE 1
#define VGA_COLOR_RED 4
#define VGA_COLOR_WHITE 15


#endif