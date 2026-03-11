#include "terminal_window.h"

#include "graphics.h"
#include "vga.h" // still needed for video_draw_text right now
#include "shell.h"
#include "../lib/string.h"
#include "../mem/heap.h"

// Font assumptions (matches your existing title centering math)
#define TERM_CHAR_W 8
#define TERM_CHAR_H 16

static void term_clear_cells(terminal_window_t* term) {
    if (!term || !term->cells) return;
    // Blank the entire grid so the renderer never reads uninitialized memory.
    memset(term->cells, ' ', (size_t)(term->cols * term->rows));
    term->cursor_col = 0;
    term->cursor_row = 0;
}

static void term_scroll_up(terminal_window_t* term) {
    if (!term || !term->cells) return;
    if (term->rows <= 1) return;

    // Move rows 1..end up to 0..end-1
    int row_bytes = term->cols;
    memcpy(term->cells, term->cells + row_bytes, (size_t)(row_bytes * (term->rows - 1)));
    // Clear the last row so newly exposed pixels always draw as blank space.
    memset(term->cells + row_bytes * (term->rows - 1), ' ', (size_t)row_bytes);

    if (term->cursor_row > 0) term->cursor_row--;
}

static void terminal_window_draw(window_t* win, void* user) {
    terminal_window_t* term = (terminal_window_t*)user;
    if (!term || !win) return;

    int cx, cy, cw, ch;
    window_get_content_rect(win, &cx, &cy, &cw, &ch);

    // Content background
    // Important: Always overwrite the full content rect every paint.
    // Using the solid fill avoids any "first scroll" artifacts if alpha blending
    // or draw-target state leaves pixels unchanged.
    graphics_fill_rect(cx, cy, cw, ch, term->bg, false, 0, false);

    // Draw characters
    // Render as 1-char strings to reuse existing text routine.
    char s[2] = {0, 0};

    int max_rows = term->rows;
    int max_cols = term->cols;

    // Simple padding
    int pad_x = 6;
    int pad_y = 6;

    int base_x = cx + pad_x;
    int base_y = cy + pad_y;

    // Also explicitly overwrite the exact text cell area (inside padding). This is
    // the region most likely to reveal stale pixels when the terminal first scrolls.
    int text_w = max_cols * TERM_CHAR_W;
    int text_h = max_rows * TERM_CHAR_H;
    if (text_w > 0 && text_h > 0) {
        if (base_x + text_w > cx + cw) text_w = (cx + cw) - base_x;
        if (base_y + text_h > cy + ch) text_h = (cy + ch) - base_y;
        if (text_w > 0 && text_h > 0) {
            graphics_fill_rect(base_x, base_y, text_w, text_h, term->bg, false, 0, false);
        }
    }

    for (int r = 0; r < max_rows; r++) {
        int y = base_y + r * TERM_CHAR_H;
        // quick reject if outside content
        if (y + TERM_CHAR_H > cy + ch) break;

        for (int c = 0; c < max_cols; c++) {
            int x = base_x + c * TERM_CHAR_W;
            if (x + TERM_CHAR_W > cx + cw) break;

            char chv = term->cells[r * max_cols + c];
            if (chv == ' ') continue;
            s[0] = chv;
            video_draw_text(x, y, s, term->fg);
        }
    }

    // Cursor (invert block)
    int cur_x = base_x + term->cursor_col * TERM_CHAR_W;
    int cur_y = base_y + term->cursor_row * TERM_CHAR_H;
    if (cur_x + TERM_CHAR_W <= cx + cw && cur_y + TERM_CHAR_H <= cy + ch) {
        graphics_fill_rect_alpha(cur_x, cur_y, TERM_CHAR_W, TERM_CHAR_H, term->fg, 255, false, 0, false);
    char under = term->cells[term->cursor_row * max_cols + term->cursor_col];
    // Cells are space-filled, but keep this defensive fallback anyway.
    if (under == 0) under = ' ';
    s[0] = (under == ' ') ? '_' : under;
    video_draw_text(cur_x, cur_y, s, term->bg);
    }
}

static void terminal_window_on_char_input(window_t* win, char c, void* user) {
    (void)win;
    terminal_window_t* term = (terminal_window_t*)user;
    if (!term) return;

    // Shell owns echo + command execution + prompt emission.
    // Terminal window is display-only: it just renders whatever the shell outputs
    // via the shell output sink.
    shell_update(c);
}

terminal_window_t* terminal_window_create(int x, int y, int width, int height, const char* title) {
    terminal_window_t* term = (terminal_window_t*)kmalloc(sizeof(terminal_window_t));
    if (!term) return 0;
    memset(term, 0, sizeof(terminal_window_t));

    term->fg = 0xFF00FF00; // green
    term->bg = 0xFF000000; // black

    term->win = window_create(x, y, width, height, title);
    if (!term->win) {
        kfree(term);
        return 0;
    }

    // Compute grid size based on content rect
    int cx, cy, cw, ch;
    window_get_content_rect(term->win, &cx, &cy, &cw, &ch);

    int pad_x = 6;
    int pad_y = 6;

    term->cols = (cw - pad_x * 2) / TERM_CHAR_W;
    term->rows = (ch - pad_y * 2) / TERM_CHAR_H;
    if (term->cols < 1) term->cols = 1;
    if (term->rows < 1) term->rows = 1;

    term->cells = (char*)kmalloc((size_t)(term->cols * term->rows));
    if (!term->cells) {
        window_close(term->win);
        kfree(term);
        return 0;
    }

    term_clear_cells(term);

    // Register as window content renderer so it always draws on top of chrome.
    window_set_content_renderer(term->win, terminal_window_draw, term);

    // Focus-based input routing: when this window is focused, keyboard chars go here.
    window_set_char_input_handler(term->win, terminal_window_on_char_input, term);

    // No initial prompt here.
    // The shell prints the prompt (using settings username) to whatever output sink is active.

    return term;
}

void terminal_window_destroy(terminal_window_t* term) {
    if (!term) return;

    if (term->win) {
        window_close(term->win);
        term->win = 0;
    }

    if (term->cells) {
        kfree(term->cells);
        term->cells = 0;
    }

    kfree(term);
}

void terminal_window_input(terminal_window_t* term, char c) {
    if (!term || !term->cells) return;

    if (c == '\r') return;

    if (c == '\n') {
        term->cursor_col = 0;
        term->cursor_row++;
        if (term->cursor_row >= term->rows) {
            term_scroll_up(term);
            term->cursor_row = term->rows - 1;
        }
        return;
    }

    if (c == '\b') {
        if (term->cursor_col > 0) {
            term->cursor_col--;
        } else if (term->cursor_row > 0) {
            term->cursor_row--;
            term->cursor_col = term->cols - 1;
        }
        term->cells[term->cursor_row * term->cols + term->cursor_col] = ' ';
        return;
    }

    // printable
    if ((unsigned char)c < 32) return;

    term->cells[term->cursor_row * term->cols + term->cursor_col] = c;
    term->cursor_col++;

    if (term->cursor_col >= term->cols) {
        term->cursor_col = 0;
        term->cursor_row++;
        if (term->cursor_row >= term->rows) {
            term_scroll_up(term);
            term->cursor_row = term->rows - 1;
        }
    }
}

void terminal_window_shell_putc(char c, void* user) {
    terminal_window_t* term = (terminal_window_t*)user;
    terminal_window_input(term, c);
}
