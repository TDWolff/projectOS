#include "vga.h"
#include "psf.h"
#include "../include/multiboot2.h"
#include "../fs/initrd.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../mem/vmm.h"
#include "shell.h"

static uint32_t* fb_addr = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;
static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;
static uint32_t fg_color = 0xFFFFFFFF; 
static uint32_t bg_color = 0x00808080; // Default to Teal background

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

    // Boundary logic for Windowed Shell
    uint32_t area_x = 0;
    uint32_t area_y = 0;
    uint32_t area_w = fb_width;
    uint32_t area_h = fb_height;

    // Use SHELL constants if they exist (we'll assume they do or use defaults)
    // Note: In a real system we'd pass a 'window' context, 
    // but for now we'll just hardcode the check.
    if (cursor_x >= 200 && cursor_x <= 800 && cursor_y >= 150 && cursor_y <= 550) {
        area_x = 205; // 5px padding
        area_y = 180; // Below title bar
        area_w = 590;
        area_h = 365;
    }

    if (c == '\n') {
        cursor_y += loaded_font.height;
        cursor_x = area_x;
    } else if (c == '\b') {
        // Backspace handling
        if (cursor_x > area_x) {
            cursor_x -= 8; // Assuming 8px wide font
            draw_rect(cursor_x, cursor_y, 8, loaded_font.height, bg_color);
        }
    } else {
        uint8_t* glyph = (uint8_t*)loaded_font.glyph_buffer + (c * loaded_font.bytes_per_glyph);
        
        for (uint32_t cy = 0; cy < loaded_font.height; cy++) {
            for (uint32_t cx = 0; cx < 8; cx++) {
                if (glyph[cy] & (0x80 >> cx)) {
                    putpixel(cursor_x + cx, cursor_y + cy, fg_color);
                }
            }
        }
        cursor_x += 8;
    }

    // Wrap text within the area
    if (cursor_x >= area_x + area_w) {
        cursor_x = area_x;
        cursor_y += loaded_font.height;
    }

    // SCROLLING: If we hit the bottom of the area, move up
    if (cursor_y >= area_y + area_h - loaded_font.height) {
        // Simplified: just reset to top of window for now
        cursor_y = area_y;
        // In a full OS we'd blit the window up
        draw_rect(area_x, area_y, area_w, area_h, 0x000000); 
    }
}

void kprint(const char* str) {
    for (size_t i = 0; str[i] != '\0'; i++) kprint_char(str[i]);
}

void terminal_clear() {
    if (!fb_addr) return;
    draw_rect(0, 0, fb_width, fb_height, bg_color);
    // Position cursor at shell window start
    cursor_x = 205; 
    cursor_y = 185;
}

void terminal_set_bg(uint32_t color) {
    bg_color = color;
}

void video_set_cursor(int x, int y) {
    cursor_x = x;
    cursor_y = y;
}

void video_set_color(uint32_t fg, uint32_t bg) {
    fg_color = fg;
    bg_color = bg;
}

uint32_t get_fb_width() { return fb_width; }
uint32_t get_fb_height() { return fb_height; }

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

void* get_framebuffer_addr() { return fb_addr; }
uint32_t get_fb_pitch() { return fb_pitch; }

void video_get_info(fb_info_t* info) {
    info->addr = (uint64_t)fb_addr;
    info->width = fb_width;
    info->height = fb_height;
    info->pitch = fb_pitch;
}

void video_draw_text(int x, int y, const char* str, uint32_t color) {
    uint32_t old_x = cursor_x;
    uint32_t old_y = cursor_y;
    uint32_t old_fg = fg_color;

    cursor_x = x;
    cursor_y = y;
    fg_color = color;

    kprint(str);

    cursor_x = old_x;
    cursor_y = old_y;
    fg_color = old_fg;
}

void video_draw_desktop() {
    if (!fb_addr) return;
    
    // Classic Windows 95 Teal: #008080
    uint32_t desktop_color = 0x008080; 
    uint32_t taskbar_color = 0xC0C0C0; 

    // 1. Fill the screen with the wallpaper color
    draw_rect(0, 0, fb_width, fb_height, desktop_color);

    // 2. Draw a Taskbar at the bottom (40 pixels high)
    draw_rect(0, fb_height - 40, fb_width, 40, taskbar_color);

    // 3. Draw a "Start" button (Darker Gray)
    // We'll draw it slightly "raised"
    draw_rect(5, fb_height - 35, 80, 30, 0xD0D0D0); // Main body
    draw_rect(5, fb_height - 35, 80, 2, 0xFFFFFF);  // Top highlight
    draw_rect(5, fb_height - 35, 2, 30, 0xFFFFFF);  // Left highlight

    // Draw Custom Terminal Icon (16x13)
    // 0: Black, 1: Gray (0x808080), 2: White (0xFFFFFF), 3: Transparent (0xD0D0D0)
    int icon_w = 16;
    int icon_h = 13;
    int icon_data[13][16] = {
        {3,3,1,1,1,1,1,1,1,1,1,1,1,1,3,3},
        {3,1,0,0,0,0,0,0,0,0,0,0,0,0,1,3},
        {1,0,2,2,0,0,0,0,0,0,0,0,0,0,0,1}, // "C"
        {1,0,2,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,2,2,0,0,2,0,2,0,0,0,0,0,0,1}, // ":\" 
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,2,2,2,2,2,2,2,2,0,0,0,0,0,1}, // Text lines
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,2,2,2,2,2,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,2,2,2,0,0,0,0,0,0,0,0,0,0,1}, 
        {3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3},
        {3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3}
    };

    int icon_x = 5 + (80 - icon_w) / 2; // Center in button (width 80)
    int icon_y = (fb_height - 35) + (30 - icon_h) / 2; // Center in button (height 30)

    for (int y = 0; y < icon_h; y++) {
        for (int x = 0; x < icon_w; x++) {
            uint32_t color = 0xD0D0D0;
            if (icon_data[y][x] == 0) color = 0x000000;
            else if (icon_data[y][x] == 1) color = 0x808080;
            else if (icon_data[y][x] == 2) color = 0xFFFFFF;
            
            putpixel(icon_x + x, icon_y + y, color);
        }
    }

    // Reset cursor for shell text (in the middle of the screen window)
    cursor_x = 205;
    cursor_y = 185;
}

void video_blit_8x8(int x, int y, uint32_t* data) {
    if (!fb_addr) return;
    // fb_pitch is in bytes, fb_addr is uint32_t*
    uint8_t* screen_ptr = (uint8_t*)fb_addr + (y * fb_pitch) + (x * 4);
    
    for (int i = 0; i < 8; i++) {
        uint32_t* line_dst = (uint32_t*)screen_ptr;
        for (int j = 0; j < 8; j++) {
            line_dst[j] = data[i * 8 + j];
        }
        screen_ptr += fb_pitch;
    }
}

uint32_t video_get_pixel(int x, int y) {
    if (!fb_addr || x < 0 || x >= (int)fb_width || y < 0 || y >= (int)fb_height) return 0;
    return *(uint32_t*)((uint8_t*)fb_addr + (y * fb_pitch) + (x * 4));
}