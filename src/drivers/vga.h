#ifndef VGA_H
#define VGA_H
#include "../include/types.h"

void video_init(void* mb_info);
void putpixel(int x, int y, uint32_t color);
void putpixel_alpha(int x, int y, uint32_t color); // Added
uint32_t getpixel(int x, int y);
void draw_rect(int x, int y, int w, int h, uint32_t color);
void terminal_initialize();
void terminal_clear();
void terminal_set_color(uint8_t fg, uint8_t bg);
void terminal_set_bg(uint32_t color);
void video_set_cursor(int x, int y);
void video_set_color(uint32_t fg, uint32_t bg);
void video_draw_text(int x, int y, const char* str, uint32_t color);
void kprint(const char* str);
void kprint_char(char c);

void* get_framebuffer_addr();
uint32_t get_fb_pitch();
uint32_t get_fb_width();
uint32_t get_fb_height();

void video_blit_8x8(int x, int y, uint32_t* data);
uint32_t video_get_pixel(int x, int y);

// Direct Buffer Targeting (For Compositor)
void video_set_subsystem_target(uint32_t* target);

// Framebuffer Info for User Space
typedef struct {
    uint64_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
} fb_info_t;

// Pass display info to user applications
void video_get_info(fb_info_t* info);
void video_draw_desktop();

// Flush the backbuffer to the screen
void video_swap();

#define VGA_COLOR_BLACK 0
#define VGA_COLOR_BLUE 1
#define VGA_COLOR_RED 4
#define VGA_COLOR_WHITE 15


#endif