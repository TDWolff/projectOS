#include "mouse.h"
#include "vga.h"
#include "../include/ports.h"

static uint8_t mouse_cycle = 0;
static uint8_t mouse_byte[3];
static int mouse_x = 512;
static int mouse_y = 384;
static int mouse_scale = 18; // Scale in units of 0.1 (18 = 1.8x)

// 1 or 2 = White, 0 = Transparent, 3 = Black Outline
static const uint8_t cursor_mask[12][8] = {
    {3,3,0,0,0,0,0,0},
    {3,1,3,0,0,0,0,0},
    {3,1,1,3,0,0,0,0},
    {3,1,1,1,3,0,0,0},
    {3,1,1,1,1,3,0,0},
    {3,1,1,1,1,1,3,0},
    {3,1,1,1,1,1,1,3},
    {3,1,1,1,3,3,3,3},
    {3,1,3,1,3,0,0,0},
    {3,3,0,3,1,3,0,0},
    {0,0,0,0,3,3,0,0},
    {0,0,0,0,0,0,0,0}
};

#define MAX_WIDTH 8
#define MAX_HEIGHT 12
#define MAX_SCALE 4
static uint32_t bg_buffer[(MAX_WIDTH * MAX_SCALE) * (MAX_HEIGHT * MAX_SCALE)]; 

/* --- Internal Hardware Helpers (Must be above init) --- */

void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) { // Data
        while (timeout--) {
            if ((inb(0x64) & 1) == 1) return;
        }
    } else { // Signal
        while (timeout--) {
            if ((inb(0x64) & 2) == 0) return;
        }
    }
}

void mouse_write(uint8_t write) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, write);
}

uint8_t mouse_read() {
    mouse_wait(0);
    return inb(0x60);
}

/* --- Internal Graphics Helpers --- */

void save_bg(int x, int y) {
    int sw = (8 * mouse_scale) / 10;
    int sh = (12 * mouse_scale) / 10;
    for (int i = 0; i < sh; i++) {
        for (int j = 0; j < sw; j++) {
            bg_buffer[i * sw + j] = video_get_pixel(x + j, y + i);
        }
    }
}

void restore_bg(int x, int y) {
    int sw = (8 * mouse_scale) / 10;
    int sh = (12 * mouse_scale) / 10;
    for (int i = 0; i < sh; i++) {
        for (int j = 0; j < sw; j++) {
            putpixel(x + j, y + i, bg_buffer[i * sw + j]);
        }
    }
}

void draw_cursor(int x, int y) {
    int sw = (8 * mouse_scale) / 10;
    int sh = (12 * mouse_scale) / 10;
    
    for (int i = 0; i < sh; i++) {
        for (int j = 0; j < sw; j++) {
            // Map the current pixel back to the 8x12 mask using fixed point
            int src_x = (j * 10) / mouse_scale;
            int src_y = (i * 10) / mouse_scale;
            
            // Bounds check for the mask
            if (src_x >= 8) src_x = 7;
            if (src_y >= 12) src_y = 11;

            uint8_t color_type = cursor_mask[src_y][src_x];
            if (color_type == 0) continue;
            
            uint32_t color = (color_type == 1) ? 0xFFFFFFFF : 0x00000000;
            putpixel(x + j, y + i, color);
        }
    }
}

int mouse_get_x() { return mouse_x; }
int mouse_get_y() { return mouse_y; }
uint8_t mouse_get_buttons() { return mouse_byte[0] & 0x07; }

void mouse_set_scale(int scale_x10) {
    if (scale_x10 < 5) scale_x10 = 5;      // 0.5x minimum
    if (scale_x10 > 40) scale_x10 = 40;    // 4.0x maximum
    
    // Clean up old cursor
    restore_bg(mouse_x, mouse_y);
    mouse_scale = scale_x10;
    // Draw new scaled cursor
    save_bg(mouse_x, mouse_y);
    draw_cursor(mouse_x, mouse_y);
}

/* --- Public Functions --- */

void mouse_init() {
    uint8_t status;

    mouse_wait(1);
    outb(0x64, 0xA8); // Enable auxiliary device

    mouse_wait(1);
    outb(0x64, 0x20); // Get status
    mouse_wait(0);
    status = (inb(0x60) | 2); // Enable IRQ 12
    mouse_wait(1);
    outb(0x64, 0x60); // Set status
    mouse_wait(1);
    outb(0x60, status);

    mouse_write(0xF6); // Set default settings
    mouse_read();
    mouse_write(0xF4); // Enable data reporting
    mouse_read();

    // Initial capture and draw
    save_bg(mouse_x, mouse_y);
    draw_cursor(mouse_x, mouse_y);
}

void mouse_handler() {
    uint8_t status = inb(0x64);
    if (!(status & 0x21)) return;

    uint8_t data = inb(0x60);

    switch(mouse_cycle) {
        case 0:
            if (!(data & 0x08)) return; // Sync check
            mouse_byte[0] = data;
            mouse_cycle++;
            break;
        case 1:
            mouse_byte[1] = data;
            mouse_cycle++;
            break;
        case 2:
            mouse_byte[2] = data;
            
            // Restore what was under the old mouse
            restore_bg(mouse_x, mouse_y);

            // Update mouse movement
            int x_offset = (int8_t)mouse_byte[1];
            int y_offset = (int8_t)mouse_byte[2];

            mouse_x += x_offset;
            mouse_y -= y_offset; // PS/2 Y is inverted

            // Clamp to screen bounds (assuming 1024x768)
            int cursor_w = (8 * mouse_scale) / 10;
            int cursor_h = (12 * mouse_scale) / 10;

            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x > (int)get_fb_width() - cursor_w) mouse_x = get_fb_width() - cursor_w;
            if (mouse_y > (int)get_fb_height() - cursor_h) mouse_y = get_fb_height() - cursor_h;

            // Save new background and draw cursor
            save_bg(mouse_x, mouse_y);
            draw_cursor(mouse_x, mouse_y);

            mouse_cycle = 0;
            break;
    }
}