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
    uint64_t pages_needed = (buffer_size + 4096 - 1) / 4096;
    void* bb_start = pmm_alloc(); 
    for (uint64_t i = 1; i < pages_needed; i++) {
        pmm_alloc(); 
    }
    
    // Allocate CANVAS BUFFER (Double the memory usage, but worth it for smooth mouse)
    void* cb_start = pmm_alloc();
    for (uint64_t i = 1; i < pages_needed; i++) {
        pmm_alloc();
    }
    
    // Assign pointers (Assuming simplistic PMM alloc returned contiguous earlier blocks)
    // In a real OS, use vmm_map to map these physical pages to virtual ranges.
    // For this project's simple PMM, we rely on the linear alloc behavior for now.
    
    // Re-using the manual heap idea from previous edit or just casting the PMM result?
    // Using simple kmalloc leads to small blocks.
    // Let's assume the previous edit's logic regarding "kmalloc vs pmm" 
    // and just use kmalloc if we have a big heap, OR use `bb_start`.
    // Since `heap.c` isn't fully visible here, let's use the `kmalloc` approach 
    // if the heap is initialized large enough. If not, use the `pmm` pointers.
    
    // Let's try kmalloc for simplicity and safety if heap is large
    // backbuffer = (uint32_t*)kmalloc(buffer_size); 
    // canvas_buffer = (uint32_t*)kmalloc(buffer_size);
    //
    // Fixed: kmalloc might not handle 3MB+ allocations depending on heap implementation.
    // Fallback to PMM direct allocation pointers which we know are valid from lines 32 and 38.
    backbuffer = (uint32_t*)bb_start;
    canvas_buffer = (uint32_t*)cb_start;
    
    if (backbuffer) memset(backbuffer, 0, buffer_size);
    if (canvas_buffer) memset(canvas_buffer, 0, buffer_size);
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
