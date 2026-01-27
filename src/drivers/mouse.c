#include "mouse.h"
#include "vga.h"
#include "../include/ports.h"

static uint8_t mouse_cycle = 0;
static uint8_t mouse_byte[3];
static int mouse_x = 512;
static int mouse_y = 384;

// Buffer to save the 8x8 pixels "under" the mouse so we can restore them
static uint32_t mouse_back_buffer[64];

/* --- Internal Helpers --- */

// Wait for the PS/2 controller to be ready
void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) { // Data ready to be read
        while (timeout--) {
            if ((inb(0x64) & 0x01) == 1) return;
        }
    } else { // Ready to receive a command
        while (timeout--) {
            if ((inb(0x64) & 0x02) == 0) return;
        }
    }
}

// Write a command to the mouse
void mouse_write(uint8_t data) {
    mouse_wait(1);
    outb(0x64, 0xD4); // Tell controller we are talking to the mouse
    mouse_wait(1);
    outb(0x60, data);
}

// Read data from the mouse
uint8_t mouse_read() {
    mouse_wait(0);
    return inb(0x60);
}

// Capture the pixels under the cursor
void save_mouse_back(int x, int y) {
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            mouse_back_buffer[i * 8 + j] = getpixel(x + j, y + i);
        }
    }
}

// Put the saved pixels back on the screen
void restore_mouse_back(int x, int y) {
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            putpixel(x + j, y + i, mouse_back_buffer[i * 8 + j]);
        }
    }
}

/* --- Public Functions --- */

void mouse_init() {
    uint8_t status;

    // Enable the auxiliary mouse device
    mouse_wait(1);
    outb(0x64, 0xA8);

    // Enable interrupts
    mouse_wait(1);
    outb(0x64, 0x20);
    mouse_wait(0);
    status = (inb(0x60) | 2);
    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, status);

    // Set defaults and enable reporting
    mouse_write(0xF6);
    mouse_read();
    mouse_write(0xF4);
    mouse_read();

    // Initial background save and draw
    save_mouse_back(mouse_x, mouse_y);
    draw_rect(mouse_x, mouse_y, 8, 8, 0xFFFFFF);
}

void mouse_handler() {
    uint8_t status = inb(0x64);
    
    // Check if data is ready and if it's actually from the mouse (bit 5)
    if (!(status & 0x01) || !(status & 0x20)) return;

    uint8_t data = inb(0x60);

    switch(mouse_cycle) {
        case 0:
            mouse_byte[0] = data;
            if (!(data & 0x08)) return; // Sync bit must be 1
            mouse_cycle++;
            break;
        case 1:
            mouse_byte[1] = data;
            mouse_cycle++;
            break;
        case 2:
            mouse_byte[2] = data;
            
            // Calculate deltas
            int x_move = (int)mouse_byte[1];
            int y_move = (int)mouse_byte[2];

            // Sign bit handling
            if (mouse_byte[0] & 0x10) x_move -= 256;
            if (mouse_byte[0] & 0x20) y_move -= 256;

            // 1. Restore what was under the old position
            restore_mouse_back(mouse_x, mouse_y);

            // 2. Update coordinates
            mouse_x += x_move;
            mouse_y -= y_move;

            // 3. Keep cursor on screen (1024x768)
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x > 1016) mouse_x = 1016;
            if (mouse_y > 760) mouse_y = 760;

            // 4. Save what is currently under the NEW position
            save_mouse_back(mouse_x, mouse_y);

            // 5. Draw the cursor (white box)
            draw_rect(mouse_x, mouse_y, 8, 8, 0xFFFFFF);

            mouse_cycle = 0;
            break;
    }
}