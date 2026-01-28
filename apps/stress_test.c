#include "libapp.h"

void _start() {
    sys_kprintf("Initializing Graphics Stress Test...\n");

    fb_info_t fb;
    sys_get_fb_info(&fb);

    if (fb.addr == 0) {
        sys_kprintf("Error: Could not get framebuffer info.\n");
        sys_exit();
    }

    uint32_t* screen = (uint32_t*)fb.addr;
    uint32_t width = fb.width;
    uint32_t height = fb.height;
    
    // Simple animated plasma/noise effect
    for (int frame = 0; frame < 500; frame++) {
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                // Generate a "pattern" based on x, y, and frame
                uint8_t r = (x + frame) % 256;
                uint8_t g = (y + frame) % 256;
                uint8_t b = (x + y + frame) % 256;
                
                // BGR format usually for 32-bit FB
                screen[y * (fb.pitch / 4) + x] = (r << 16) | (g << 8) | b;
            }
        }
    }

    sys_kprintf("Stress Test Complete.\n");
    sys_exit();
}