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

#define VGA_COLOR_BLACK 0
#define VGA_COLOR_BLUE 1
#define VGA_COLOR_RED 4
#define VGA_COLOR_WHITE 15


#endif