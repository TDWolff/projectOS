#ifndef TERMINAL_WINDOW_H
#define TERMINAL_WINDOW_H

#include "window.h"
#include "../include/types.h"

// Terminal font metrics (must match renderer assumptions).
#define TERM_CHAR_W 8
#define TERM_CHAR_H 16

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
    char*    cells;       // visible grid (view into scrollback)
    uint8_t* cells_attrs; // per-cell attribute byte; bit 0 = reverse video

    // Scrollback (in lines). Grows up to `scrollback_capacity` and then
    // behaves like a ring where the oldest lines are dropped.
    int      scrollback_capacity;
    int      scrollback_count;
    int      scrollback_start;
    char*    scrollback;
    uint8_t* scrollback_attrs; // attribute buffer parallel to scrollback

    // How far up from the bottom we're currently viewing.
    // 0 = follow the latest output (default).
    int scroll_offset;

    // ANSI / VT100 escape sequence parser
    uint8_t ansi_state;    // 0=NORMAL 1=ESC 2=CSI
    char    ansi_buf[32];  // parameter bytes
    uint8_t ansi_len;
    uint8_t ansi_attr;     // current SGR attribute: 0=normal 1=reverse-video
} terminal_window_t;

// Creates a new terminal window instance and registers it as the window content renderer.
terminal_window_t* terminal_window_create(int x, int y, int width, int height, const char* title);

// Destroys the terminal window state and closes the window.
void terminal_window_destroy(terminal_window_t* term);

// Feeds a single character of input (already decoded from keyboard handler).
void terminal_window_input(terminal_window_t* term, char c);

// Scroll the terminal viewport. Positive delta scrolls up (older history).
// Negative delta scrolls down (toward newest output).
void terminal_window_scroll(terminal_window_t* term, int delta_lines);

// Adapter suitable for shell_set_output_sink: forwards bytes into terminal.
void terminal_window_shell_putc(char c, void* user);

// Query the usable character grid dimensions.
void terminal_window_get_size(terminal_window_t* term, int* rows, int* cols);

#endif
