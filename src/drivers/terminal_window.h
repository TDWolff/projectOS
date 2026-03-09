#ifndef TERMINAL_WINDOW_H
#define TERMINAL_WINDOW_H

#include "window.h"
#include "../include/types.h"

// A tiny window-hosted terminal renderer.
// Owns its own character grid and draws inside a window's content rect.

typedef struct {
    window_t* win;

    // character grid
    int cols;
    int rows;
    int cursor_col;
    int cursor_row;

    uint32_t fg;
    uint32_t bg;

    // row-major: rows * cols
    char* cells;
} terminal_window_t;

// Creates a new terminal window instance and registers it as the window content renderer.
terminal_window_t* terminal_window_create(int x, int y, int width, int height, const char* title);

// Destroys the terminal window state and closes the window.
void terminal_window_destroy(terminal_window_t* term);

// Feeds a single character of input (already decoded from keyboard handler).
void terminal_window_input(terminal_window_t* term, char c);

// Adapter suitable for shell_set_output_sink: forwards bytes into terminal.
void terminal_window_shell_putc(char c, void* user);

#endif
