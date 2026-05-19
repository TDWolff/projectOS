#include "terminal_window.h"

#include "graphics.h"
#include "vga.h"
#include "shell.h"
#include "../lib/string.h"
#include "../mem/heap.h"

#define TERM_SCROLLBACK_DEFAULT_CAPACITY 256

static inline int term_scrollback_index(const terminal_window_t* term, int logical_row) {
    int idx = term->scrollback_start + logical_row;
    if (idx >= term->scrollback_capacity) idx -= term->scrollback_capacity;
    return idx;
}

static void term_scrollback_clear_row(terminal_window_t* term, int physical_row) {
    memset(term->scrollback + physical_row * term->cols, ' ', (size_t)term->cols);
    if (term->scrollback_attrs)
        memset(term->scrollback_attrs + physical_row * term->cols, 0, (size_t)term->cols);
}

static void term_scrollback_append_blank_line(terminal_window_t* term) {
    if (!term || !term->scrollback) return;

    if (term->scrollback_count < term->scrollback_capacity) {
        int pr = term_scrollback_index(term, term->scrollback_count);
        term_scrollback_clear_row(term, pr);
        term->scrollback_count++;
        return;
    }

    term->scrollback_start++;
    if (term->scrollback_start >= term->scrollback_capacity) term->scrollback_start = 0;
    int pr = term_scrollback_index(term, term->scrollback_count - 1);
    term_scrollback_clear_row(term, pr);
}

static void term_sync_visible_cells(terminal_window_t* term) {
    if (!term || !term->cells || !term->scrollback) return;

    int max_offset = term->scrollback_count - term->rows;
    if (max_offset < 0) max_offset = 0;
    if (term->scroll_offset < 0) term->scroll_offset = 0;
    if (term->scroll_offset > max_offset) term->scroll_offset = max_offset;

    int bottom_first_row = term->scrollback_count - term->rows;
    if (bottom_first_row < 0) bottom_first_row = 0;
    int first_row = bottom_first_row - term->scroll_offset;
    if (first_row < 0) first_row = 0;

    for (int r = 0; r < term->rows; r++) {
        int sr = first_row + r;
        if (sr < 0 || sr >= term->scrollback_count) {
            memset(term->cells + r * term->cols, ' ', (size_t)term->cols);
            if (term->cells_attrs)
                memset(term->cells_attrs + r * term->cols, 0, (size_t)term->cols);
            continue;
        }

        int pr = term_scrollback_index(term, sr);
        memcpy(term->cells + r * term->cols,
               term->scrollback + pr * term->cols, (size_t)term->cols);
        if (term->cells_attrs && term->scrollback_attrs)
            memcpy(term->cells_attrs + r * term->cols,
                   term->scrollback_attrs + pr * term->cols, (size_t)term->cols);
    }
}

static void term_clear_cells(terminal_window_t* term) {
    if (!term || !term->cells) return;
    memset(term->cells, ' ', (size_t)(term->cols * term->rows));
    if (term->cells_attrs)
        memset(term->cells_attrs, 0, (size_t)(term->cols * term->rows));
    term->cursor_col = 0;
    term->cursor_row = 0;

    if (term->scrollback) {
        memset(term->scrollback, ' ',
               (size_t)(term->scrollback_capacity * term->cols));
        if (term->scrollback_attrs)
            memset(term->scrollback_attrs, 0,
                   (size_t)(term->scrollback_capacity * term->cols));
        term->scrollback_start = 0;
        term->scrollback_count = 0;
        term->scroll_offset = 0;
        term_scrollback_append_blank_line(term);
        term_sync_visible_cells(term);
    }
}

// Full clear for editors: also pre-allocates `rows` blank lines so that
// absolute cursor positioning (ESC[r;cH) works immediately after the clear.
static void term_clear_for_editor(terminal_window_t* term) {
    if (!term || !term->scrollback) return;
    memset(term->scrollback, ' ',
           (size_t)(term->scrollback_capacity * term->cols));
    if (term->scrollback_attrs)
        memset(term->scrollback_attrs, 0,
               (size_t)(term->scrollback_capacity * term->cols));
    term->scrollback_start = 0;
    term->scrollback_count = 0;
    term->scroll_offset    = 0;
    term->ansi_attr        = 0;

    for (int i = 0; i < term->rows; i++)
        term_scrollback_append_blank_line(term);

    term->cursor_row = 0;
    term->cursor_col = 0;
    term_sync_visible_cells(term);
}

static void term_scroll_up(terminal_window_t* term) {
    if (!term) return;
    term_scrollback_append_blank_line(term);
    if (term->scroll_offset == 0)
        term_sync_visible_cells(term);
}

// ---------------------------------------------------------------------------
// ANSI / VT100 CSI handler
// ---------------------------------------------------------------------------

static int ansi_parse_int(const char* p) {
    int n = 0;
    while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
    return n;
}

static void term_set_cursor_vis(terminal_window_t* term, int vis_row, int vis_col) {
    if (vis_row < 0) vis_row = 0;
    if (vis_col < 0) vis_col = 0;
    if (vis_row >= term->rows) vis_row = term->rows - 1;
    if (vis_col >= term->cols) vis_col = term->cols - 1;

    int bottom_first = term->scrollback_count - term->rows;
    if (bottom_first < 0) bottom_first = 0;
    term->cursor_row = bottom_first + vis_row;
    term->cursor_col = vis_col;
}

static void term_handle_csi(terminal_window_t* term, const char* params, char cmd) {
    switch (cmd) {
    case 'J': // Erase in display
        if (params[0] == '2')
            term_clear_for_editor(term);
        break;

    case 'H': // Cursor position ESC[row;colH  (1-indexed)
    case 'f': {
        int row = 1, col = 1;
        const char* p = params;
        if (*p) {
            row = ansi_parse_int(p);
            while (*p && *p != ';') p++;
            if (*p == ';') {
                p++;
                col = ansi_parse_int(p);
            }
        }
        if (row < 1) row = 1;
        if (col < 1) col = 1;
        term_set_cursor_vis(term, row - 1, col - 1);
        break;
    }

    case 'm': // SGR
        if (params[0] == '7')
            term->ansi_attr = 1; // reverse video
        else
            term->ansi_attr = 0; // normal
        break;

    case 'K': // Erase in line
        if (term->cursor_row >= 0 && term->cursor_row < term->scrollback_count) {
            int pr   = term_scrollback_index(term, term->cursor_row);
            char* row_chars = term->scrollback + pr * term->cols;
            uint8_t* row_attrs = term->scrollback_attrs
                                 ? term->scrollback_attrs + pr * term->cols : 0;
            int from = (params[0] == '2') ? 0 : term->cursor_col;
            int len  = term->cols - from;
            if (len > 0) {
                memset(row_chars + from, ' ', (size_t)len);
                if (row_attrs) memset(row_attrs + from, 0, (size_t)len);
            }
        }
        if (term->scroll_offset == 0) term_sync_visible_cells(term);
        break;

    case 'A': { // Cursor up
        int n = params[0] ? ansi_parse_int(params) : 1;
        if (n < 1) n = 1;
        int bottom_first = term->scrollback_count - term->rows;
        if (bottom_first < 0) bottom_first = 0;
        int vis = term->cursor_row - bottom_first - n;
        if (vis < 0) vis = 0;
        term->cursor_row = bottom_first + vis;
        break;
    }
    case 'B': { // Cursor down
        int n = params[0] ? ansi_parse_int(params) : 1;
        if (n < 1) n = 1;
        int bottom_first = term->scrollback_count - term->rows;
        if (bottom_first < 0) bottom_first = 0;
        int vis = term->cursor_row - bottom_first + n;
        if (vis >= term->rows) vis = term->rows - 1;
        term->cursor_row = bottom_first + vis;
        break;
    }
    case 'C': { // Cursor right
        int n = params[0] ? ansi_parse_int(params) : 1;
        if (n < 1) n = 1;
        term->cursor_col += n;
        if (term->cursor_col >= term->cols) term->cursor_col = term->cols - 1;
        break;
    }
    case 'D': { // Cursor left
        int n = params[0] ? ansi_parse_int(params) : 1;
        if (n < 1) n = 1;
        term->cursor_col -= n;
        if (term->cursor_col < 0) term->cursor_col = 0;
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

static void terminal_window_draw(window_t* win, void* user) {
    terminal_window_t* term = (terminal_window_t*)user;
    if (!term || !win) return;

    int cx, cy, cw, ch;
    window_get_content_rect(win, &cx, &cy, &cw, &ch);

    graphics_fill_rect(cx, cy, cw, ch, term->bg, false, 0, false);

    char s[2] = {0, 0};
    int max_rows = term->rows;
    int max_cols = term->cols;
    int pad_x = 6;
    int pad_y = 6;
    int base_x = cx + pad_x;
    int base_y = cy + pad_y;

    int text_w = max_cols * TERM_CHAR_W;
    int text_h = max_rows * TERM_CHAR_H;
    if (text_w > 0 && text_h > 0) {
        if (base_x + text_w > cx + cw) text_w = (cx + cw) - base_x;
        if (base_y + text_h > cy + ch) text_h = (cy + ch) - base_y;
        if (text_w > 0 && text_h > 0)
            graphics_fill_rect(base_x, base_y, text_w, text_h, term->bg,
                               false, 0, false);
    }

    term_sync_visible_cells(term);

    for (int r = 0; r < max_rows; r++) {
        int y = base_y + r * TERM_CHAR_H;
        if (y + TERM_CHAR_H > cy + ch) break;

        for (int c = 0; c < max_cols; c++) {
            int x = base_x + c * TERM_CHAR_W;
            if (x + TERM_CHAR_W > cx + cw) break;

            char chv = term->cells[r * max_cols + c];
            uint8_t attr = term->cells_attrs
                           ? term->cells_attrs[r * max_cols + c] : 0;
            bool rev = (attr & 1) != 0;

            if (rev) {
                // Draw reverse-video background rect, then draw char in bg color
                graphics_fill_rect(x, y, TERM_CHAR_W, TERM_CHAR_H, term->fg,
                                   false, 0, false);
                s[0] = (chv == ' ' || chv == 0) ? ' ' : chv;
                if (s[0] != ' ')
                    video_draw_text(x, y, s, term->bg);
            } else {
                if (chv == ' ' || chv == 0) continue;
                s[0] = chv;
                video_draw_text(x, y, s, term->fg);
            }
        }
    }

    // Cursor (only when at bottom of scrollback)
    if (term->scroll_offset == 0) {
        int bottom_first = term->scrollback_count - term->rows;
        if (bottom_first < 0) bottom_first = 0;
        int vis_r = term->cursor_row - bottom_first;
        if (vis_r >= 0 && vis_r < term->rows) {
            int cur_x = base_x + term->cursor_col * TERM_CHAR_W;
            int cur_y = base_y + vis_r * TERM_CHAR_H;
            if (cur_x + TERM_CHAR_W <= cx + cw && cur_y + TERM_CHAR_H <= cy + ch) {
                graphics_fill_rect_alpha(cur_x, cur_y, TERM_CHAR_W, TERM_CHAR_H,
                                         term->fg, 255, false, 0, false);
                char under = term->cells[vis_r * max_cols + term->cursor_col];
                if (under == 0) under = ' ';
                s[0] = (under == ' ') ? '_' : under;
                video_draw_text(cur_x, cur_y, s, term->bg);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

static void terminal_window_on_char_input(window_t* win, char c, void* user) {
    (void)win;
    terminal_window_t* term = (terminal_window_t*)user;
    if (!term) return;
    shell_update(c);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

terminal_window_t* terminal_window_create(int x, int y, int width, int height,
                                           const char* title) {
    terminal_window_t* term = (terminal_window_t*)kmalloc(sizeof(terminal_window_t));
    if (!term) return 0;
    memset(term, 0, sizeof(terminal_window_t));

    term->fg = 0xFF00FF00;
    term->bg = 0xFF000000;

    term->win = window_create(x, y, width, height, title);
    if (!term->win) { kfree(term); return 0; }

    int cx, cy, cw, ch;
    window_get_content_rect(term->win, &cx, &cy, &cw, &ch);

    int pad_x = 6, pad_y = 6;
    term->cols = (cw - pad_x * 2) / TERM_CHAR_W;
    term->rows = (ch - pad_y * 2) / TERM_CHAR_H;
    if (term->cols < 1) term->cols = 1;
    if (term->rows < 1) term->rows = 1;

    term->cells = (char*)kmalloc((size_t)(term->cols * term->rows));
    if (!term->cells) { window_close(term->win); kfree(term); return 0; }

    term->cells_attrs = (uint8_t*)kmalloc((size_t)(term->cols * term->rows));
    if (!term->cells_attrs) { kfree(term->cells); window_close(term->win); kfree(term); return 0; }

    term->scrollback_capacity = TERM_SCROLLBACK_DEFAULT_CAPACITY;
    if (term->scrollback_capacity < term->rows)
        term->scrollback_capacity = term->rows;

    term->scrollback = (char*)kmalloc(
        (size_t)(term->scrollback_capacity * term->cols));
    if (!term->scrollback) {
        kfree(term->cells_attrs); kfree(term->cells);
        window_close(term->win); kfree(term); return 0;
    }

    term->scrollback_attrs = (uint8_t*)kmalloc(
        (size_t)(term->scrollback_capacity * term->cols));
    if (!term->scrollback_attrs) {
        kfree(term->scrollback); kfree(term->cells_attrs); kfree(term->cells);
        window_close(term->win); kfree(term); return 0;
    }

    term_clear_cells(term);

    window_set_content_renderer(term->win, terminal_window_draw, term);
    window_set_char_input_handler(term->win, terminal_window_on_char_input, term);

    return term;
}

void terminal_window_destroy(terminal_window_t* term) {
    if (!term) return;
    if (term->win)             { window_close(term->win); term->win = 0; }
    if (term->cells)           { kfree(term->cells); term->cells = 0; }
    if (term->cells_attrs)     { kfree(term->cells_attrs); term->cells_attrs = 0; }
    if (term->scrollback)      { kfree(term->scrollback); term->scrollback = 0; }
    if (term->scrollback_attrs){ kfree(term->scrollback_attrs); term->scrollback_attrs = 0; }
    kfree(term);
}

void terminal_window_input(terminal_window_t* term, char c) {
    if (!term || !term->cells) return;

    // --- ANSI escape parser ---
    if (term->ansi_state == 1) {
        if (c == '[') {
            term->ansi_state = 2;
            term->ansi_len   = 0;
            term->ansi_buf[0]= 0;
        } else {
            term->ansi_state = 0;
        }
        return;
    }
    if (term->ansi_state == 2) {
        if (c >= 0x40 && c <= 0x7E) {
            term->ansi_buf[term->ansi_len] = 0;
            term_handle_csi(term, term->ansi_buf, c);
            term->ansi_state = 0;
        } else if (term->ansi_len < 31) {
            term->ansi_buf[term->ansi_len++] = c;
        }
        return;
    }
    if (c == '\x1B') {
        term->ansi_state = 1;
        return;
    }

    // --- Regular character handling ---
    if (c == '\r') return;

    if (c == '\n') {
        term->cursor_col = 0;
        term->cursor_row++;
        if (term->cursor_row >= term->scrollback_count) term_scroll_up(term);
        if (term->cursor_row >= term->scrollback_count)
            term->cursor_row = term->scrollback_count - 1;
        if (term->scroll_offset == 0) {
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
        if (term->scrollback && term->cursor_row >= 0 &&
            term->cursor_row < term->scrollback_count) {
            int pr = term_scrollback_index(term, term->cursor_row);
            term->scrollback[pr * term->cols + term->cursor_col] = ' ';
            if (term->scrollback_attrs)
                term->scrollback_attrs[pr * term->cols + term->cursor_col] = 0;
        }
        if (term->scroll_offset == 0) term_sync_visible_cells(term);
        return;
    }

    if ((unsigned char)c < 32) return;

    while (term->cursor_row >= term->scrollback_count)
        term_scrollback_append_blank_line(term);

    int pr = term_scrollback_index(term, term->cursor_row);
    term->scrollback[pr * term->cols + term->cursor_col] = c;
    if (term->scrollback_attrs)
        term->scrollback_attrs[pr * term->cols + term->cursor_col] =
            term->ansi_attr;
    term->cursor_col++;

    if (term->cursor_col >= term->cols) {
        term->cursor_col = 0;
        term->cursor_row++;
        if (term->cursor_row >= term->scrollback_count) term_scroll_up(term);
    }

    if (term->scroll_offset == 0) term_sync_visible_cells(term);
}

void terminal_window_scroll(terminal_window_t* term, int delta_lines) {
    if (!term || delta_lines == 0) return;
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
    if (term && term->win) window_mark_dirty(term->win);
}

void terminal_window_get_size(terminal_window_t* term, int* rows, int* cols) {
    if (!term) { if (rows) *rows = 24; if (cols) *cols = 80; return; }
    if (rows) *rows = term->rows;
    if (cols) *cols = term->cols;
}
