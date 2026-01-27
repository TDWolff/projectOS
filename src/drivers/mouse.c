#include "mouse.h"
#include "vga.h"
#include "../include/ports.h"

static uint8_t mouse_cycle = 0;
static uint8_t mouse_byte[3];
static int mouse_x = 512;
static int mouse_y = 384;

// Two buffers for flicker-free rendering
static uint32_t bg_buffer[64];   // Stores the pixels that were there before the mouse
static uint32_t comp_buffer[64]; // Temporary buffer to "draw" the cursor onto the background

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
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            bg_buffer[i * 8 + j] = video_get_pixel(x + j, y + i);
        }
    }
}

void restore_bg(int x, int y) {
    video_blit_8x8(x, y, bg_buffer);
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
    
    // Create initial cursor (white crosshair)
    for(int i=0; i<64; i++) comp_buffer[i] = 0xFFFFFFFF; 
    video_blit_8x8(mouse_x, mouse_y, comp_buffer);
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
            mouse_cycle = 0;

            int x_move = (int)mouse_byte[1];
            int y_move = (int)mouse_byte[2];
            if (mouse_byte[0] & 0x10) x_move -= 256;
            if (mouse_byte[0] & 0x20) y_move -= 256;

            // 1. Restore the OLD background
            restore_bg(mouse_x, mouse_y);

            // 2. Update coordinates
            mouse_x += x_move;
            mouse_y -= y_move;

            // Screen boundaries
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x > 1015) mouse_x = 1015;
            if (mouse_y > 759) mouse_y = 759;

            // 3. Save the NEW background
            save_bg(mouse_x, mouse_y);

            // 4. Create the Composition (Background + Cursor)
            for(int i=0; i<64; i++) comp_buffer[i] = bg_buffer[i];
            
            // Draw a simple 8x8 white crosshair into the comp_buffer
            for(int i=0; i<8; i++) {
                comp_buffer[i*8 + 4] = 0xFFFFFFFF; // Vertical
                comp_buffer[4*8 + i] = 0xFFFFFFFF; // Horizontal
            }

            // 5. Blit the entire thing to the screen in one go
            video_blit_8x8(mouse_x, mouse_y, comp_buffer);
            break;
    }
}