#include "mouse.h"
#include "vga.h"
#include "../include/ports.h"

static uint8_t mouse_cycle = 0;
static uint8_t mouse_byte[3];
static int mouse_x = 512;
static int mouse_y = 384;
static int mouse_scale = 10; // Modified to 1.0x (10/10) default, ignored in new draw

#define MAX_WIDTH 16
#define MAX_HEIGHT 24

// 1 = White, 0 = Transparent, 3 = Black Outline
static const uint8_t cursor_mask[MAX_HEIGHT][MAX_WIDTH] = {
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,3,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,3,3,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,3,3,3,1,0,0,0,0,0,0,0,0,0,0,0},
    {1,3,3,3,3,1,0,0,0,0,0,0,0,0,0,0},
    {1,3,3,3,3,3,1,0,0,0,0,0,0,0,0,0},
    {1,3,3,3,3,3,3,1,0,0,0,0,0,0,0,0},
    {1,3,3,3,3,3,3,3,1,0,0,0,0,0,0,0},
    {1,3,3,3,3,3,3,3,3,1,0,0,0,0,0,0},
    {1,3,3,3,3,3,3,3,3,3,1,0,0,0,0,0},
    {1,3,3,3,3,3,3,3,3,3,3,1,0,0,0,0},
    {1,3,3,3,3,3,3,3,3,3,3,3,1,0,0,0},
    {1,3,3,3,3,3,3,1,1,1,1,1,1,0,0,0},
    {1,3,3,3,3,3,3,1,0,0,0,0,0,0,0,0},
    {1,3,3,1,1,3,3,3,1,0,0,0,0,0,0,0},
    {1,3,1,0,0,1,3,3,3,1,0,0,0,0,0,0},
    {1,1,0,0,0,1,3,3,3,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,3,3,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,3,3,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

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

// New function to draw directly to a compositing buffer (bypassing the global putpixel)
void mouse_draw_to_buffer(uint32_t* buffer, uint32_t pitch, uint32_t bpp_div_8) {
    // Determine screen boundary to prevent overflow
    // Assuming video_get_width/height isn't available here easily without include cycle,
    // we rely on the clipping being done by caller relative to 'buffer' size if possible.
    // However, raw buffer write needs some care.
    
    for (int i = 0; i < MAX_HEIGHT; i++) {
        for (int j = 0; j < MAX_WIDTH; j++) {
            uint8_t color_type = cursor_mask[i][j];
            if (color_type == 0) continue; // Transparent
            
            uint32_t color = (color_type == 1) ? 0xFFFFFFFF : 0x00000000;
            
            // Calculate memory offset
            // Buffer is uint32_t*, but pitch is in bytes
            // Address = buffer_base + (y * pitch) + (x * bytes_per_pixel)
            
            // Check boundaries
            int screen_w = (int)get_fb_width();
            int screen_h = (int)get_fb_height();

            if (mouse_x + j >= screen_w || mouse_y + i >= screen_h || mouse_x + j < 0 || mouse_y + i < 0) {
                continue;
            }

            uint64_t offset = ((mouse_y + i) * pitch) + ((mouse_x + j) * bpp_div_8);
            
            // Simple bound check (assuming 1080p roughly max or trusting valid memory)
            // A real driver would need fb_width/height passed in
            
            uint32_t* pixel = (uint32_t*)((uint8_t*)buffer + offset);
            *pixel = color;
        }
    }
}

int mouse_get_x() { return mouse_x; }
int mouse_get_y() { return mouse_y; }
uint8_t mouse_get_buttons() { return mouse_byte[0] & 0x07; }

void mouse_set_scale(int scale_x10) {
    if (scale_x10 < 5) scale_x10 = 5;      // 0.5x minimum
    if (scale_x10 > 40) scale_x10 = 40;    // 4.0x maximum
    mouse_scale = scale_x10;
    // No need to redraw here, next frame will pick it up
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

    // No initial draw needed, compositor loop handles it
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
            
            // Note: No restore_bg() calls needed anymore!
            
            // Update mouse movement
            int x_offset = (int8_t)mouse_byte[1];
            int y_offset = (int8_t)mouse_byte[2];

            mouse_x += x_offset;
            mouse_y -= y_offset; // PS/2 Y is inverted

            // Clamp to screen bounds
            int w = (int)get_fb_width();
            int h = (int)get_fb_height();
            
            // Mouse tip should stay within screen
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= w) mouse_x = w - 1;
            if (mouse_y >= h) mouse_y = h - 1;

            // No draw_cursor() needed!
            
            mouse_cycle = 0;
            break;
    }
}