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
            if (color_type == 0) continue; // Transparent
            
            uint32_t color = (color_type == 1) ? 0xFFFFFFFF : 0x00000000;
            
            // Calculate memory offset
            // Buffer is uint32_t*, but pitch is in bytes
            // Address = buffer_base + (y * pitch) + (x * bytes_per_pixel)
            uint64_t offset = ((mouse_y + i) * pitch) + ((mouse_x + j) * bpp_div_8);
            
            // We need to be careful not to write out of bounds of the buffer
            // (Assuming caller guarantees buffer size or we check limits? 
            // The handler clamps X/Y so we should be mostly safe, but minimal check:)
            // Note: We don't have buffer height here easily, trusting clamping.
            
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

            // Clamp to screen bounds (assuming 1024x768 - should query properly but hardcoded for now)
            // TODO: Get actual screen dimensions
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x > 1024 - 10) mouse_x = 1024 - 10;
            if (mouse_y > 768 - 10) mouse_y = 768 - 10;

            // No draw_cursor() needed!
            
            mouse_cycle = 0;
            break;
    }
}