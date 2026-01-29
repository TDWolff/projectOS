#include "compositor.h"
#include "vga.h"
#include "../include/ports.h" // Added for io_wait
#include "../mem/heap.h"
#include "../lib/string.h"
#include "../mem/pmm.h"
#include "mouse.h"

// Tri-Buffer Architecture:
// 1. canvas_buffer: The clean desktop where apps draw (Returned by get_backbuffer)
// 2. backbuffer: The composition surface (Canvas + Mouse)
// 3. frontbuffer: The video memory

static uint32_t* backbuffer = 0; 
static uint32_t* canvas_buffer = 0; // New

static uint32_t screen_width = 0;
static uint32_t screen_height = 0;
static uint32_t screen_pitch = 0;

void compositor_init(uint32_t width, uint32_t height, uint32_t pitch) {
    screen_width = width;
    screen_height = height;
    screen_pitch = pitch;

    // Calculate buffer size in bytes
    // pitch is bytes per row. 
    uint64_t buffer_size = (uint64_t)height * pitch;
    
    // Allocate BACKBUFFER
    backbuffer = (uint32_t*)kmalloc(buffer_size);
    if (!backbuffer) {
        // If kmalloc fails, we're in trouble, but for now let's hope the new heap size works
        // Fallback or panic could go here
    }
    memset((void*)backbuffer, 0, buffer_size);

    // Allocate CANVAS BUFFER
    canvas_buffer = (uint32_t*)kmalloc(buffer_size);
    if (!canvas_buffer) {
        // Panic
    }
    memset((void*)canvas_buffer, 0, buffer_size);
}

// Get the address we should be drawing TO
// All applications and the shell draw faithfully to the "Clean Canvas"
void* compositor_get_backbuffer() {
    return canvas_buffer;
}

// Copy Back Buffer -> Front Buffer (Video Memory)
void compositor_swap_buffers() {
    void* frontbuffer = get_framebuffer_addr();
    if (!backbuffer || !frontbuffer || !canvas_buffer) return;

    // 1. Composition Step:
    // Copy Clean Canvas -> Backbuffer (Overwrites previous mouse/garbage)
    memcpy(backbuffer, canvas_buffer, screen_height * screen_pitch);

    // 2. Draw Mouse on Backbuffer
    // We draw "on top" of the fresh copy of the desktop
    mouse_draw_to_buffer(backbuffer, screen_pitch, 4); // 4 bytes per pixel (32bpp)

    // Critical Section: Disable interrupts to prevent interference during V-Sync/Swap
    __asm__ volatile("cli");

    // NEW: Wait for Vertical Retrace to avoid tearing (V-Sync)
    while ((inb(0x3DA) & 0x08)); 
    while (!(inb(0x3DA) & 0x08));

    // 3. Presentation Step:
    // Fast copy Backbuffer -> Frontbuffer (Video Memory)
    memcpy(frontbuffer, backbuffer, screen_height * screen_pitch);

    // Re-enable interrupts
    __asm__ volatile("sti");
}
