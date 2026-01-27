#include "vga.h"
#include "psf.h"
#include "../include/multiboot2.h"
#include "../fs/initrd.h"
#include "../lib/stdio.h"
#include "../lib/string.h"

static uint32_t* fb_addr = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;
static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;
static uint32_t fg_color = 0xFFFFFFFF; 
static uint32_t bg_color = 0x00000000; 

static font_t loaded_font = {0};

void putpixel(int x, int y, uint32_t color) {
    if (!fb_addr || x < 0 || x >= (int)fb_width || y < 0 || y >= (int)fb_height) return;
    uint32_t* pixel = (uint32_t*)((uint8_t*)fb_addr + (y * fb_pitch) + (x * 4));
    *pixel = color;
}

// New: Draw a filled rectangle
void draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            putpixel(x + j, y + i, color);
        }
    }
}

// New: Move all pixels up by one font height
void terminal_scroll() {
    if (!fb_addr || !loaded_font.height) return;

    uint64_t font_height = loaded_font.height;
    uint8_t* dst = (uint8_t*)fb_addr;
    uint8_t* src = (uint8_t*)fb_addr + (font_height * fb_pitch);
    uint64_t size_to_copy = (fb_height - font_height) * fb_pitch;

    // Move the screen up
    memcpy(dst, src, size_to_copy);

    // Clear the bottom line
    uint32_t* bottom_line = (uint32_t*)((uint8_t*)fb_addr + (fb_height - font_height) * fb_pitch);
    for (uint32_t i = 0; i < (font_height * fb_pitch) / 4; i++) {
        bottom_line[i] = bg_color;
    }

    cursor_y -= font_height;
}

void video_init(void* mb_info) {
    struct multiboot_tag* tag;
    for (tag = (struct multiboot_tag*)((uint8_t*)mb_info + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) 
    {
        if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
            struct multiboot_tag_framebuffer_common* fb = (struct multiboot_tag_framebuffer_common*)tag;
            fb_addr = (uint32_t*)fb->framebuffer_addr;
            fb_width = fb->framebuffer_width;
            fb_height = fb->framebuffer_height;
            fb_pitch = fb->framebuffer_pitch;
            break;
        }
    }
    if (!fb_addr) return;

    terminal_clear();

    // Font detection
    file_t* all_files = initrd_get_files();
    for (int i = 0; i < MAX_FILES; i++) {
        if (all_files[i].exists) {
            uint8_t* raw = (uint8_t*)all_files[i].address;
            if (raw[0] == 0x72 && raw[1] == 0xb5 && raw[2] == 0x4a && raw[3] == 0x86) {
                psf2_header_t* hdr = (psf2_header_t*)raw;
                loaded_font.glyph_buffer = (void*)(raw + hdr->headersize);
                loaded_font.width = hdr->width;
                loaded_font.height = hdr->height;
                loaded_font.bytes_per_glyph = hdr->charsize;
                return;
            }
            else if (raw[0] == 0x36 && raw[1] == 0x04) {
                psf1_header_t* hdr = (psf1_header_t*)raw;
                loaded_font.glyph_buffer = (void*)(raw + sizeof(psf1_header_t));
                loaded_font.width = 8;
                loaded_font.height = hdr->charsize;
                loaded_font.bytes_per_glyph = hdr->charsize;
                return;
            }
        }
    }
}

void kprint_char(char c) {
    if (!fb_addr || !loaded_font.glyph_buffer) return;

    if (c == '\n') {
        cursor_x = 0;
        cursor_y += loaded_font.height;
    } else if (c == '\b') {
        if (cursor_x >= loaded_font.width) {
            cursor_x -= loaded_font.width;
            draw_rect(cursor_x, cursor_y, loaded_font.width, loaded_font.height, bg_color);
        }
    } else {
        uint8_t* glyph = (uint8_t*)loaded_font.glyph_buffer + (c * loaded_font.bytes_per_glyph);
        uint32_t bytes_per_row = (loaded_font.width + 7) / 8;

        for (uint32_t y = 0; y < loaded_font.height; y++) {
            for (uint32_t x = 0; x < loaded_font.width; x++) {
                if ((glyph[y * bytes_per_row + (x / 8)] >> (7 - (x % 8))) & 1)
                    putpixel(cursor_x + x, cursor_y + y, fg_color);
                else
                    putpixel(cursor_x + x, cursor_y + y, bg_color);
            }
        }
        cursor_x += loaded_font.width;
        if (cursor_x >= fb_width) { cursor_x = 0; cursor_y += loaded_font.height; }
    }

    // SCROLLING: If we hit the bottom, move the screen up
    if (cursor_y >= fb_height - loaded_font.height) {
        terminal_scroll();
    }
}

void kprint(const char* str) {
    for (size_t i = 0; str[i] != '\0'; i++) kprint_char(str[i]);
}

void terminal_clear() {
    if (!fb_addr) return;
    draw_rect(0, 0, fb_width, fb_height, bg_color);
    cursor_x = 0;
    cursor_y = 0;
}

uint32_t getpixel(int x, int y) {
    if (!fb_addr || x < 0 || x >= (int)fb_width || y < 0 || y >= (int)fb_height) return 0;
    return *(uint32_t*)((uint8_t*)fb_addr + (y * fb_pitch) + (x * 4));
}

void terminal_initialize() {}
void terminal_set_color(uint8_t fg, uint8_t bg) { 
    // Simplified conversion for now
    if (fg == 15) fg_color = 0xFFFFFFFF;
    if (fg == 4)  fg_color = 0xFFFF0000;
    if (bg == 1)  bg_color = 0x000000FF;
    else bg_color = 0;
}