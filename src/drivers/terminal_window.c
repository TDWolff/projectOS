#include "terminal_window.h"

#include "graphics.h"
#include "vga.h" // still needed for video_draw_text right now
#include "shell.h"
#include "../lib/string.h"
#include "../mem/heap.h"

// Font assumptions are defined in `terminal_window.h`.

// Scrollback size in lines (only as far as this window existed).
// Memory: capacity * rows * cols bytes. Example: 256 lines * 120 cols = ~30KB.
#define TERM_SCROLLBACK_DEFAULT_CAPACITY 256

static inline int term_scrollback_index(const terminal_window_t* term, int logical_row) {
    // logical_row: 0..scrollback_count-1
    int idx = term->scrollback_start + logical_row;
    if (idx >= term->scrollback_capacity) idx -= term->scrollback_capacity;
    return idx;
}

static void term_scrollback_clear_row(terminal_window_t* term, int physical_row) {
    memset(term->scrollback + physical_row * term->cols, ' ', (size_t)term->cols);
}

static void term_scrollback_append_blank_line(terminal_window_t* term) {
    if (!term || !term->scrollback) return;

    if (term->scrollback_count < term->scrollback_capacity) {
        int pr = term_scrollback_index(term, term->scrollback_count);
        term_scrollback_clear_row(term, pr);
        term->scrollback_count++;
        return;
    }

    // Full: drop oldest (advance start), reuse the freed physical row for the new one.
    term->scrollback_start++;
    if (term->scrollback_start >= term->scrollback_capacity) term->scrollback_start = 0;
    // Count stays at capacity.
    int pr = term_scrollback_index(term, term->scrollback_count - 1);
    term_scrollback_clear_row(term, pr);
}

static void term_sync_visible_cells(terminal_window_t* term) {
    if (!term || !term->cells || !term->scrollback) return;

    // Clamp offset.
    int max_offset = term->scrollback_count - term->rows;
    if (max_offset < 0) max_offset = 0;
    if (term->scroll_offset < 0) term->scroll_offset = 0;
    if (term->scroll_offset > max_offset) term->scroll_offset = max_offset;

    int bottom_first_row = term->scrollback_count - term->rows;
    if (bottom_first_row < 0) bottom_first_row = 0;
    int first_row = bottom_first_row - term->scroll_offset;
    if (first_row < 0) first_row = 0;

    // Copy scrollback -> visible grid.
    for (int r = 0; r < term->rows; r++) {
        int sr = first_row + r;
        if (sr < 0 || sr >= term->scrollback_count) {
            memset(term->cells + r * term->cols, ' ', (size_t)term->cols);
            continue;
        }

        int pr = term_scrollback_index(term, sr);
        memcpy(term->cells + r * term->cols, term->scrollback + pr * term->cols, (size_t)term->cols);
    }
}

static void term_clear_cells(terminal_window_t* term) {
    if (!term || !term->cells) return;
    // Blank the entire grid so the renderer never reads uninitialized memory.
    memset(term->cells, ' ', (size_t)(term->cols * term->rows));
    term->cursor_col = 0;
    term->cursor_row = 0;

    // Clear scrollback too.
    if (term->scrollback) {
        memset(term->scrollback, ' ', (size_t)(term->scrollback_capacity * term->cols));
        term->scrollback_start = 0;
        term->scrollback_count = 0;
        term->scroll_offset = 0;
        // Ensure at least one line exists.
        term_scrollback_append_blank_line(term);
        term_sync_visible_cells(term);
    }
}

static void term_scroll_up(terminal_window_t* term) {
    // Terminal "scroll" is now represented by adding a new blank line to scrollback.
    // Visible scrolling is handled via `scroll_offset`.
    if (!term) return;
    term_scrollback_append_blank_line(term);

    // When at bottom-follow mode, keep viewport pinned to bottom.
    if (term->scroll_offset == 0) {
        term_sync_visible_cells(term);
    }
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

    // Ensure cells reflect current scrollback view.
    term_sync_visible_cells(term);

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

    // Cursor (only when following bottom; when scrolled up we hide it).
    if (term->scroll_offset == 0) {
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

    term->scrollback_capacity = TERM_SCROLLBACK_DEFAULT_CAPACITY;
    if (term->scrollback_capacity < term->rows) term->scrollback_capacity = term->rows;
    term->scrollback = (char*)kmalloc((size_t)(term->scrollback_capacity * term->cols));
    if (!term->scrollback) {
        kfree(term->cells);
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

    if (term->scrollback) {
        kfree(term->scrollback);
        term->scrollback = 0;
    }

    kfree(term);
}

void terminal_window_input(terminal_window_t* term, char c) {
    if (!term || !term->cells) return;

    if (c == '\r') return;

    if (c == '\n') {
        term->cursor_col = 0;
        term->cursor_row++;

        // If user was viewing history, keep their scroll offset while new output arrives.
        // If they were at bottom, remain at bottom.
        if (term->cursor_row >= term->scrollback_count) {
            term_scroll_up(term);
        }

        // Clamp cursor to visible area for rendering when at bottom.
        if (term->cursor_row >= term->scrollback_count) term->cursor_row = term->scrollback_count - 1;

        // Map cursor_row (in scrollback space) to visible row when following bottom.
        if (term->scroll_offset == 0) {
            // Keep cursor pinned within visible grid.
            int bottom_first_row = term->scrollback_count - term->rows;
            if (bottom_first_row < 0) bottom_first_row = 0;
            int vis_row = term->cursor_row - bottom_first_row;
            if (vis_row < 0) vis_row = 0;
            if (vis_row >= term->rows) vis_row = term->rows - 1;
            term->cursor_row = bottom_first_row + vis_row;
            term_sync_visible_cells(term);
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
        // Apply backspace to scrollback line.
        if (term->scrollback && term->cursor_row >= 0 && term->cursor_row < term->scrollback_count) {
            int pr = term_scrollback_index(term, term->cursor_row);
            term->scrollback[pr * term->cols + term->cursor_col] = ' ';
        }
        if (term->scroll_offset == 0) term_sync_visible_cells(term);
        return;
    }

    // printable
    if ((unsigned char)c < 32) return;

    // Ensure the current scrollback line exists.
    while (term->cursor_row >= term->scrollback_count) {
        term_scrollback_append_blank_line(term);
    }

    // Write to scrollback.
    int pr = term_scrollback_index(term, term->cursor_row);
    term->scrollback[pr * term->cols + term->cursor_col] = c;
    term->cursor_col++;

    if (term->cursor_col >= term->cols) {
        term->cursor_col = 0;
        term->cursor_row++;
        if (term->cursor_row >= term->scrollback_count) {
            term_scroll_up(term);
        }
    }

    if (term->scroll_offset == 0) term_sync_visible_cells(term);
}

void terminal_window_scroll(terminal_window_t* term, int delta_lines) {
    if (!term) return;
    if (delta_lines == 0) return;

    term->scroll_offset += delta_lines;

    int max_offset = term->scrollback_count - term->rows;
    if (max_offset < 0) max_offset = 0;
    if (term->scroll_offset < 0) term->scroll_offset = 0;
    if (term->scroll_offset > max_offset) term->scroll_offset = max_offset;

    term_sync_visible_cells(term);
    if (term->win) window_mark_dirty(term->win);
}

void terminal_window_shell_putc(char c, void* user) {
    terminal_window_t* term = (terminal_window_t*)user;
    terminal_window_input(term, c);
    // Mark the window dirty so the compositor re-renders on the next frame.
    if (term && term->win) window_mark_dirty(term->win);
}
