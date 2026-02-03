#include "compositor.h"
#include "vga.h"
#include "../include/ports.h" // Added for io_wait
#include "../mem/heap.h"
#include "../lib/string.h"
#include "../mem/pmm.h"
#include "mouse.h"
#include "keyboard.h"
#include "mouse.h"
#include "../lib/settings.h"
#include "vga.h" // Added video
#include "window.h" // Added window manager

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

// Internal function to redraw the entire scene stack
// 1. Desktop Background (Wallpaper/Color)
// 2. Windows (Back to Front)
// 3. Mouse Cursor
static void compositor_render_scene() {
    // 1. Clear Backbuffer with Desktop (copy from Canvas)
    // Optimization: If no windows moved, we don't strictly need to do this, 
    // but for now we redraw every frame for correctness during movement.
    uint64_t buffer_size = (uint64_t)screen_height * screen_pitch;
    
    // Copy the static background (Shell/Wallpaper) into the composition buffer
    memcpy(backbuffer, canvas_buffer, buffer_size);

    // 2. Draw Windows
    // We need to tell the window manager to draw onto the *backbuffer*, 
    // but currently all drawing functions target the "active" buffer.
    // We temporarily swap the target, or ensure window_draw uses putpixel logic 
    // that targets the backbuffer. 
    
    // Ideally, we'd pass the buffer to window_draw. For now, since `putpixel` 
    // checks `get_draw_buffer()`, we need to ensure THAT returns 'backbuffer'.
    
    // TODO: This part is tricky with global state. 
    // For now, let's assume `window_paint_all` calls graphical functions 
    // that eventually write to whatever `compositor_get_draw_target()` returns.
    
    // HACK: Start drawing windows
    window_paint_all();
    
    // 3. Draw Mouse
    mouse_draw_to_buffer(backbuffer, screen_pitch, 4);
}

// Copy Back Buffer -> Front Buffer (Video Memory)
void compositor_swap_buffers() {
    void* frontbuffer = get_framebuffer_addr();
    if (!backbuffer || !frontbuffer || !canvas_buffer) return;

    // 1. Composition Step:
    // Render the full stack (BG -> Windows -> Mouse) to Backbuffer
    video_set_subsystem_target(backbuffer); // Tell graphics engine to draw here
    compositor_render_scene(); 
    video_set_subsystem_target(canvas_buffer); // Reset to canvas for normal drawing (e.g. system updates)
    
    // 2. V-Sync / Presentation Step:
    // Copy Backbuffer -> Frontbuffer (Hardware)
    memcpy(frontbuffer, backbuffer, screen_height * screen_pitch);
}
