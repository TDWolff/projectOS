#include "vga.h"
#include "psf.h"
#include "../include/multiboot2.h"
#include "../fs/initrd.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../mem/vmm.h"
#include "shell.h"
#include "compositor.h"
#include "bmp.h"
#include "png.h"

static uint32_t* fb_addr = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;
static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;
static uint32_t fg_color = 0xFFFFFFFF; 
static uint32_t bg_color = 0x00808080; // Default to Teal background

// Global override for drawing target
static uint32_t* manual_draw_target = 0;

void video_set_subsystem_target(uint32_t* target) {
    manual_draw_target = target;
}

// Helper to get draw target (Backbuffer if enabled, else Frontbuffer)
static uint32_t* get_draw_buffer() {
    if (manual_draw_target) return manual_draw_target;

    uint32_t* bb = (uint32_t*)compositor_get_backbuffer();
    if (bb) return bb;
    return fb_addr;
}

uint32_t* video_get_draw_target(void) {
    return get_draw_buffer();
}

static font_t loaded_font = {0};

// Helper to pack r,g,b into uint32
uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (r << 16) | (g << 8) | b;
}

// Fixed: getpixel should read from the same buffer we are drawing to (Double Buffering)
// This ensures we can read back what we just drew (e.g. for blur effects)
uint32_t getpixel(int x, int y) {
    uint32_t* buffer = get_draw_buffer();
    if (!buffer || x < 0 || x >= (int)fb_width || y < 0 || y >= (int)fb_height) return 0;
    // fb_pitch is in bytes. index = y * (pitch / 4) + x
    return buffer[y * (fb_pitch / 4) + x];
}

void putpixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)fb_width || y < 0 || y >= (int)fb_height) return;
    
    uint32_t* buffer = get_draw_buffer();
    if (!buffer) return;

    // fb_pitch is in bytes. index = y * (pitch / 4) + x
    buffer[y * (fb_pitch / 4) + x] = color;
}

// Draw a pixel with Alpha Blending
// Color format: 0xAARRGGBB
void putpixel_alpha(int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)fb_width || y < 0 || y >= (int)fb_height) return;

    uint32_t* buffer = get_draw_buffer();
    if (!buffer) return;

    uint8_t alpha = (color >> 24) & 0xFF;

    // Optimization: If fully opaque, just draw
    if (alpha == 255) {
        buffer[y * (fb_pitch / 4) + x] = color;
        return;
    }
    // Optimization: If fully transparent, do nothing
    if (alpha == 0) {
        return;
    }

    // Alpha Blending Math:
    // Result = (Source * Alpha + Dest * (255 - Alpha)) / 255
    uint32_t bg_color = buffer[y * (fb_pitch / 4) + x];
    
    uint8_t bg_r = (bg_color >> 16) & 0xFF;
    uint8_t bg_g = (bg_color >> 8) & 0xFF;
    uint8_t bg_b = (bg_color) & 0xFF;

    uint8_t fg_r = (color >> 16) & 0xFF;
    uint8_t fg_g = (color >> 8) & 0xFF;
    uint8_t fg_b = (color) & 0xFF;

    // Use integer math (approximate / 255 with >> 8 for speed if desired, but here is accurate)
    uint8_t out_r = (uint8_t)((fg_r * alpha + bg_r * (255 - alpha)) / 255);
    uint8_t out_g = (uint8_t)((fg_g * alpha + bg_g * (255 - alpha)) / 255);
    uint8_t out_b = (uint8_t)((fg_b * alpha + bg_b * (255 - alpha)) / 255);

    buffer[y * (fb_pitch / 4) + x] = (out_r << 16) | (out_g << 8) | out_b;
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

    // Simple scroll up by one line
    for (uint32_t y = area_y + loaded_font.height; y < area_y + area_h; y++) {
        for (uint32_t x = area_x; x < area_x + area_w; x++) {
            uint32_t color = getpixel(x, y);
            putpixel(x, y - loaded_font.height, color);
        }
    }

    // Clear the last line
    draw_rect(area_x, area_y + area_h - loaded_font.height, area_w, loaded_font.height, bg_color);

    // Adjust cursor position
    cursor_y -= loaded_font.height;
    if (cursor_y < area_y) cursor_y = area_y;
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
    
    // Initialize Double Buffering
    if (fb_addr) {
        // Initialize heap first if not already done in kernel.c, but here we assume it's ready
        // video_init is called after heap_init in kernel.c
        compositor_init(fb_width, fb_height, fb_pitch);
        
        // PNG preferred, BMP fallback — both use mode 1 (scale-to-fill, centred)
        if (!png_draw("background.png", 0, 0, 1))
            bmp_draw("background.bmp", 0, 0, 1);
    }

    if (!fb_addr) return;

    // Remove the implicit clear or handle it differently if we have a wallpaper
    // terminal_clear();

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

    // NOTE: Legacy text-mode style scrolling cleared a hard-coded rectangle.
    // The windowed terminal (`terminal_window.c`) now owns terminal scrolling.
    // Clearing here can cause a one-time black rectangle when output reaches
    // the bottom of the windowed terminal.
    if (cursor_y >= area_y + area_h - loaded_font.height) {
        cursor_y = area_y;
        // Intentionally do NOT clear any pixels here.
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
    
    // uint32_t taskbar_color = 0xC1C1C1;

    // 1. Fill the screen with the wallpaper color (FALLBACK)
    // If we have a BMP, we might want to skip this or draw it underneath
    // For now, let's just clear to Teal before drawing BMP on top.
    
    // IMPORTANT: We should NOT draw solid teal if we just drew a wallpaper!
    // But video_draw_desktop isn't currently called in the loop in kernel.c?
    // Let's check kernel.c
    // Actually, video_draw_desktop IS NOT called in kernel.c.
    
    // However, the issue described "it's back to the default original with the hand drawn taskbar"
    // implies something IS drawing over the wallpaper.
    
    // Ah, wait. 'video_draw_desktop' logic is likely invoked somewhere or "terminal_clear"
    // The previous analysis showed "terminal_clear" is called at end of video_init.
    
    // In video_init:
    // ... compositor_init ...
    // ... bmp_draw ...
    // terminal_clear(); <-- THIS WIPES THE WALLPAPER!
    
    // draw_rect(0, 0, fb_width, fb_height, desktop_color);

    // 2. Draw a Taskbar at the bottom (40 pixels high)
    // draw_rect(0, fb_height - 40, fb_width, 40, taskbar_color);

    // Reset cursor for shell text (in the middle of the screen window)
    cursor_x = 205;
    cursor_y = 185;
}

void video_blit_8x8(int x, int y, uint32_t* data) {
    uint32_t* buffer = get_draw_buffer();
    if (!buffer) return;
    
    // fb_pitch is in bytes, fb_addr is uint32_t*
    uint8_t* screen_ptr = (uint8_t*)buffer + (y * fb_pitch) + (x * 4);
    
    for (int i = 0; i < 8; i++) {
        uint32_t* line_dst = (uint32_t*)screen_ptr;
        for (int j = 0; j < 8; j++) {
            line_dst[j] = data[i * 8 + j];
        }
        screen_ptr += fb_pitch;
    }
}

// Function to flush frame
void video_swap() {
    compositor_swap_buffers();
}

// Deprecated or Alias
uint32_t video_get_pixel(int x, int y) {
    return getpixel(x, y);
}