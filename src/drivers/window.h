#ifndef WINDOW_H
#define WINDOW_H

#include "../include/types.h"

// Basic Window Structure
typedef struct window_t {
    int x;
    int y;
    int width;
    int height;
    char title[32];
    uint32_t flags;
    // Used to restore a maximized window back to its previous values.
    // Valid when WIN_FLAG_MAXIMIZED is set.
    int restore_x;
    int restore_y;
    int restore_w;
    int restore_h;
    // Optional per-window content draw callback.
    // Called after the window frame is drawn, so content appears on top.
    void (*draw_content)(struct window_t* win, void* user);
    void* draw_content_user;
    // Pointers for future use (e.g., content buffer, next window in stack)
    struct window_t* next;
} window_t;

// Create a new window object
window_t* window_create(int x, int y, int width, int height, const char* title);

// Close/destroy a window (unlink + free). Safe to call multiple times on the same
// window pointer only if the caller ensures the pointer isn't reused.
void window_close(window_t* win);

// Bring the window to the front (top-most) of the z-stack.
void window_focus(window_t* win);

// Draw the window frame (MacOS style)
void window_draw(window_t* win);

// Paint all windows in the stack (Painter's Algorithm)
void window_paint_all();

// Register a per-window content renderer.
// The callback will be invoked for that window every time it's painted,
// after chrome/body/titlebar is drawn.
void window_set_content_renderer(window_t* win, void (*draw_content)(window_t* win, void* user), void* user);

// Utility: content rectangle in screen coordinates.
void window_get_content_rect(window_t* win, int* out_x, int* out_y, int* out_w, int* out_h);

// Check if mouse interacts with any window (Dragging logic)
// To be called every frame or upon mouse event
void window_handle_mouse(int mouse_x, int mouse_y, uint8_t buttons);

#endif
