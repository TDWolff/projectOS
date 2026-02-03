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
    // Pointers for future use (e.g., content buffer, next window in stack)
    struct window_t* next;
} window_t;

// Create a new window object
window_t* window_create(int x, int y, int width, int height, const char* title);

// Draw the window frame (MacOS style)
void window_draw(window_t* win);

// Paint all windows in the stack (Painter's Algorithm)
void window_paint_all();

// Check if mouse interacts with any window (Dragging logic)
// To be called every frame or upon mouse event
void window_handle_mouse(int mouse_x, int mouse_y, uint8_t buttons);

#endif
