#include "editor.h"
#include "shell.h"
#include "../fs/pfs/pfs.h"
#include "../lib/string.h"
#include "../mem/heap.h"

// ---------------------------------------------------------------------------
// Buffer limits
// ---------------------------------------------------------------------------

#define ED_MAX_LINES  512
#define ED_LINE_LEN   256

// ---------------------------------------------------------------------------
// Editor modes
// ---------------------------------------------------------------------------

typedef enum {
    ED_MODE_NANO = 0,
    ED_MODE_VIM_NORMAL,
    ED_MODE_VIM_INSERT,
    ED_MODE_VIM_CMD,
} ed_mode_t;

// ---------------------------------------------------------------------------
// Editor state (single global instance; editors are not re-entrant)
// ---------------------------------------------------------------------------

typedef struct {
    char     lines[ED_MAX_LINES][ED_LINE_LEN];
    int      lens[ED_MAX_LINES];   // valid char count per line (excluding NUL)
    int      count;                // total lines

    int      cur_row;              // cursor row in buffer
    int      cur_col;              // cursor col in buffer (0-indexed)
    int      view_row;             // first visible buffer row

    int      term_rows;
    int      term_cols;

    char     filename[128];
    bool     modified;
    bool     quit;

    ed_mode_t mode;

    // ESC sequence accumulation (for arrow keys sent as ESC[A etc.)
    int      esc_state;   // 0=normal, 1=ESC, 2=CSI
    char     esc_buf[8];
    int      esc_len;

    // vim command-line buffer
    char     cmdline[64];
    int      cmdline_len;

    // vim: previous normal-mode char (for two-char commands like gg, dd)
    char     last_normal_key;

    // nano cut buffer (single line)
    char     cut_buf[ED_LINE_LEN];
    int      cut_len;
    bool     has_cut;

    // transient status message (e.g. save error); cleared on next keypress
    char     msg[56];
    bool     msg_is_err;

    bool     needs_redraw;
} editor_t;

static editor_t* g_ed = 0;

// ---------------------------------------------------------------------------
// ANSI output helpers
// ---------------------------------------------------------------------------

static void ed_str(const char* s) {
    for (; *s; s++) sh_putc(*s);
}

static void ed_int(int n) {
    if (n < 0) { sh_putc('-'); n = -n; }
    char buf[12]; int i = 0;
    if (n == 0) { sh_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (i--) sh_putc(buf[i]);
}

// Move cursor to 1-indexed (row, col) in terminal space.
static void ed_goto(int row, int col) {
    sh_putc('\x1B'); sh_putc('[');
    ed_int(row); sh_putc(';'); ed_int(col); sh_putc('H');
}

// Clear entire screen and reset to top-left.
static void ed_clear(void) {
    sh_putc('\x1B'); sh_putc('['); sh_putc('2'); sh_putc('J');
    ed_goto(1, 1);
}

// Erase from cursor to end of line.
static void ed_erase_eol(void) {
    sh_putc('\x1B'); sh_putc('['); sh_putc('K');
}

// Start/stop reverse video.
static void ed_rev_on(void)  { sh_putc('\x1B'); sh_putc('['); sh_putc('7'); sh_putc('m'); }
static void ed_rev_off(void) { sh_putc('\x1B'); sh_putc('['); sh_putc('0'); sh_putc('m'); }

// ---------------------------------------------------------------------------
// Buffer helpers
// ---------------------------------------------------------------------------

static void ed_ensure_line(editor_t* ed, int idx) {
    while (ed->count <= idx && ed->count < ED_MAX_LINES) {
        ed->lines[ed->count][0] = 0;
        ed->lens[ed->count]     = 0;
        ed->count++;
    }
}

static void ed_insert_char(editor_t* ed, int row, int col, char c) {
    if (row >= ED_MAX_LINES) return;
    ed_ensure_line(ed, row);
    int len = ed->lens[row];
    if (len >= ED_LINE_LEN - 1) return;
    // Shift right
    for (int i = len; i > col; i--) ed->lines[row][i] = ed->lines[row][i-1];
    ed->lines[row][col] = c;
    ed->lens[row]++;
    ed->lines[row][ed->lens[row]] = 0;
    ed->modified = true;
}

static void ed_delete_char(editor_t* ed, int row, int col) {
    if (row >= ed->count) return;
    int len = ed->lens[row];
    if (col < 0 || col >= len) return;
    for (int i = col; i < len - 1; i++) ed->lines[row][i] = ed->lines[row][i+1];
    ed->lens[row]--;
    ed->lines[row][ed->lens[row]] = 0;
    ed->modified = true;
}

// Join line[row+1] onto the end of line[row] and remove line[row+1].
static void ed_join_next(editor_t* ed, int row) {
    if (row + 1 >= ed->count) return;
    int la = ed->lens[row], lb = ed->lens[row+1];
    int avail = ED_LINE_LEN - 1 - la;
    int copy = (lb < avail) ? lb : avail;
    for (int i = 0; i < copy; i++)
        ed->lines[row][la + i] = ed->lines[row+1][i];
    ed->lens[row] = la + copy;
    ed->lines[row][ed->lens[row]] = 0;

    // Remove row+1
    for (int r = row + 1; r < ed->count - 1; r++) {
        memcpy(ed->lines[r], ed->lines[r+1], (size_t)(ed->lens[r+1] + 1));
        ed->lens[r] = ed->lens[r+1];
    }
    ed->count--;
    ed->modified = true;
}

// Split line[row] at col: remainder becomes a new line below.
static void ed_split_line(editor_t* ed, int row, int col) {
    if (ed->count >= ED_MAX_LINES) return;
    ed_ensure_line(ed, row);

    // Insert a blank line after row
    for (int r = ed->count; r > row + 1; r--) {
        memcpy(ed->lines[r], ed->lines[r-1], (size_t)(ed->lens[r-1] + 1));
        ed->lens[r] = ed->lens[r-1];
    }
    ed->count++;

    // Copy tail of current line to new line
    int len = ed->lens[row];
    int tail = len - col;
    if (tail < 0) tail = 0;
    memcpy(ed->lines[row+1], ed->lines[row] + col, (size_t)tail);
    ed->lines[row+1][tail] = 0;
    ed->lens[row+1] = tail;

    // Truncate current line
    ed->lines[row][col] = 0;
    ed->lens[row] = col;
    ed->modified = true;
}

static void ed_delete_line(editor_t* ed, int row) {
    if (row < 0 || row >= ed->count) return;
    if (ed->count == 1) {
        // Keep at least one empty line
        ed->lines[0][0] = 0;
        ed->lens[0] = 0;
        ed->modified = true;
        return;
    }
    for (int r = row; r < ed->count - 1; r++) {
        memcpy(ed->lines[r], ed->lines[r+1], (size_t)(ed->lens[r+1] + 1));
        ed->lens[r] = ed->lens[r+1];
    }
    ed->count--;
    ed->modified = true;
}

// Clamp cursor to valid position.
static void ed_clamp_cursor(editor_t* ed) {
    if (ed->cur_row < 0) ed->cur_row = 0;
    if (ed->cur_row >= ed->count) ed->cur_row = ed->count - 1;
    if (ed->cur_row < 0) ed->cur_row = 0;

    int max_col = ed->lens[ed->cur_row];
    // In insert mode cursor can be one past end; in normal mode it can't go past last char
    if (ed->mode == ED_MODE_VIM_NORMAL && max_col > 0) max_col--;
    if (ed->cur_col < 0) ed->cur_col = 0;
    if (ed->cur_col > max_col) ed->cur_col = max_col;
    if (ed->cur_col < 0) ed->cur_col = 0;
}

// Scroll view so cursor row is visible.
static void ed_scroll(editor_t* ed) {
    int vis_lines = ed->term_rows - 2; // bottom 2 rows = status bars
    if (vis_lines < 1) vis_lines = 1;
    if (ed->cur_row < ed->view_row)
        ed->view_row = ed->cur_row;
    if (ed->cur_row >= ed->view_row + vis_lines)
        ed->view_row = ed->cur_row - vis_lines + 1;
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

static bool ed_load(editor_t* ed, const char* path) {
    uint8_t* buf = 0;
    uint32_t sz  = 0;

    // path is already an absolute resolved path (e.g. /user/hello.txt).
    if (!pfs_read_user_file(path, &buf, &sz) || !buf) {
        // New file — start with one empty line
        ed->count = 1;
        ed->lines[0][0] = 0;
        ed->lens[0] = 0;
        return true; // not an error — new file
    }

    // Parse buffer into lines
    ed->count = 0;
    uint32_t start = 0;
    for (uint32_t i = 0; i <= sz; i++) {
        if (i == sz || buf[i] == '\n') {
            if (ed->count >= ED_MAX_LINES) break;
            uint32_t end = i;
            if (end > 0 && buf[end-1] == '\r') end--;
            uint32_t len = end - start;
            if (len >= ED_LINE_LEN) len = ED_LINE_LEN - 1;
            memcpy(ed->lines[ed->count], buf + start, (size_t)len);
            ed->lines[ed->count][len] = 0;
            ed->lens[ed->count] = (int)len;
            ed->count++;
            start = i + 1;
        }
    }
    if (ed->count == 0) {
        ed->count = 1;
        ed->lines[0][0] = 0;
        ed->lens[0] = 0;
    }

    kfree(buf);
    return true;
}

static bool ed_save(editor_t* ed) {
    // Estimate buffer size: sum of line lengths + newlines
    uint32_t total = 0;
    for (int i = 0; i < ed->count; i++) total += (uint32_t)ed->lens[i] + 1;
    if (total == 0) total = 1;

    uint8_t* buf = (uint8_t*)kmalloc(total);
    if (!buf) return false;

    uint32_t pos = 0;
    for (int i = 0; i < ed->count; i++) {
        int len = ed->lens[i];
        memcpy(buf + pos, ed->lines[i], (size_t)len);
        pos += (uint32_t)len;
        buf[pos++] = '\n';
    }

    // ed->filename is already an absolute resolved path.
    bool ok = pfs_write_user_file(ed->filename, buf, pos);
    kfree(buf);
    if (ok) {
        ed->modified = false;
        ed->msg[0] = 0;
    } else {
        const char* e = "Error: this location is not writable";
        int i = 0; while (e[i] && i < 55) { ed->msg[i] = e[i]; i++; } ed->msg[i] = 0;
        ed->msg_is_err = true;
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

static void ed_draw_nano(editor_t* ed) {
    ed_clear();
    int vis_lines = ed->term_rows - 2;
    if (vis_lines < 1) vis_lines = 1;

    for (int i = 0; i < vis_lines; i++) {
        int buf_row = ed->view_row + i;
        ed_goto(i + 1, 1);
        if (buf_row < ed->count) {
            int col_start = 0;
            int max_print = ed->term_cols;
            int len = ed->lens[buf_row];
            for (int c = col_start; c < len && (c - col_start) < max_print; c++)
                sh_putc(ed->lines[buf_row][c]);
        }
        ed_erase_eol();
    }

    // Status bar (second-to-last row) — reverse video
    ed_goto(ed->term_rows - 1, 1);
    ed_rev_on();
    sh_printf("  %s%s", ed->filename, ed->modified ? " [Modified]" : "");
    // Pad to fill the row
    int slen = 2 + (int)strlen(ed->filename) + (ed->modified ? 11 : 0);
    for (int i = slen; i < ed->term_cols; i++) sh_putc(' ');
    ed_rev_off();

    // Help bar (last row) — show error message if one is pending
    ed_goto(ed->term_rows, 1);
    if (ed->msg[0]) {
        ed_str(ed->msg);
    } else {
        ed_str("^S Save  ^X Exit  ^K Cut  ^U Paste");
    }
    ed_erase_eol();

    // Reposition cursor
    int vis_row = ed->cur_row - ed->view_row + 1;
    int vis_col = ed->cur_col + 1;
    if (vis_row < 1) vis_row = 1;
    if (vis_col < 1) vis_col = 1;
    ed_goto(vis_row, vis_col);
}

static void ed_draw_vim(editor_t* ed) {
    ed_clear();
    int vis_lines = ed->term_rows - 1;
    if (vis_lines < 1) vis_lines = 1;

    for (int i = 0; i < vis_lines; i++) {
        int buf_row = ed->view_row + i;
        ed_goto(i + 1, 1);
        if (buf_row < ed->count) {
            int len = ed->lens[buf_row];
            int max_print = ed->term_cols;
            for (int c = 0; c < len && c < max_print; c++)
                sh_putc(ed->lines[buf_row][c]);
        } else {
            sh_putc('~'); // empty line marker (vim-style)
        }
        ed_erase_eol();
    }

    // Status / command line (last row)
    ed_goto(ed->term_rows, 1);
    if (ed->mode == ED_MODE_VIM_CMD) {
        sh_putc(':');
        ed_str(ed->cmdline);
        ed_erase_eol();
    } else {
        ed_rev_on();
        if (ed->msg[0]) {
            ed_str(ed->msg);
        } else {
            if (ed->mode == ED_MODE_VIM_INSERT)
                ed_str("-- INSERT --");
            else
                ed_str("-- NORMAL --");
            sh_printf("  %s%s  L%d C%d",
                      ed->filename,
                      ed->modified ? " [+]" : "",
                      ed->cur_row + 1,
                      ed->cur_col + 1);
        }
        ed_erase_eol();
        ed_rev_off();
    }

    // Reposition cursor
    int vis_row = ed->cur_row - ed->view_row + 1;
    int vis_col = ed->cur_col + 1;
    if (vis_row < 1) vis_row = 1;
    if (vis_col < 1) vis_col = 1;
    if (ed->mode == ED_MODE_VIM_CMD) {
        // Cursor at end of command line
        ed_goto(ed->term_rows, 2 + ed->cmdline_len);
    } else {
        ed_goto(vis_row, vis_col);
    }
}

static void ed_draw(editor_t* ed) {
    ed_scroll(ed);
    if (ed->mode == ED_MODE_NANO)
        ed_draw_nano(ed);
    else
        ed_draw_vim(ed);
}

// ---------------------------------------------------------------------------
// ESC sequence dispatch (for arrow keys etc.)
// ---------------------------------------------------------------------------

static void ed_handle_csi(editor_t* ed, const char* params, char cmd) {
    switch (cmd) {
    case 'A': // Up
        ed->cur_row--;
        ed_clamp_cursor(ed);
        ed->needs_redraw = true;
        break;
    case 'B': // Down
        ed->cur_row++;
        ed_clamp_cursor(ed);
        ed->needs_redraw = true;
        break;
    case 'C': // Right
        ed->cur_col++;
        ed_clamp_cursor(ed);
        ed->needs_redraw = true;
        break;
    case 'D': // Left
        ed->cur_col--;
        ed_clamp_cursor(ed);
        ed->needs_redraw = true;
        break;
    case 'H': // Home
        ed->cur_col = 0;
        ed->needs_redraw = true;
        break;
    case 'F': // End
        ed->cur_col = ed->lens[ed->cur_row];
        ed_clamp_cursor(ed);
        ed->needs_redraw = true;
        break;
    case '~':
        if (params[0] == '3') { // Delete key
            if (ed->cur_col < ed->lens[ed->cur_row]) {
                ed_delete_char(ed, ed->cur_row, ed->cur_col);
            } else if (ed->cur_row + 1 < ed->count) {
                ed_join_next(ed, ed->cur_row);
            }
            ed->needs_redraw = true;
        }
        break;
    default:
        break;
    }
}

// Feed one raw char through the ESC-sequence parser.
// Returns true if the char was consumed by the parser.
static bool ed_parse_esc(editor_t* ed, char c) {
    if (ed->esc_state == 1) {
        if (c == '[') {
            ed->esc_state = 2;
            ed->esc_len   = 0;
            ed->esc_buf[0]= 0;
            return true;
        }
        ed->esc_state = 0;
        return false; // bare ESC — let caller handle
    }
    if (ed->esc_state == 2) {
        if (c >= 0x40 && c <= 0x7E) {
            ed->esc_buf[ed->esc_len] = 0;
            ed_handle_csi(ed, ed->esc_buf, c);
            ed->esc_state = 0;
        } else if (ed->esc_len < 7) {
            ed->esc_buf[ed->esc_len++] = c;
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Nano key handling
// ---------------------------------------------------------------------------

static void ed_nano_key(char c, void* user) {
    editor_t* ed = (editor_t*)user;
    if (!ed) return;

    // Clear any pending status message on keypress
    if (ed->msg[0]) { ed->msg[0] = 0; ed->msg_is_err = false; ed->needs_redraw = true; }

    // ESC sequence pass-through for arrow keys
    if (ed->esc_state > 0 || c == '\x1B') {
        if (c == '\x1B') { ed->esc_state = 1; return; }
        if (ed_parse_esc(ed, c)) return;
    }

    switch ((unsigned char)c) {
    case 0x13: // Ctrl+S — save
    case 0x0F: // Ctrl+O — save
        if (ed_save(ed)) {
            // Brief "saved" indicator — will be shown in status bar on next draw
        }
        ed->needs_redraw = true;
        break;

    case 0x18: // Ctrl+X — exit
        if (ed->modified) {
            // Auto-save on exit
            ed_save(ed);
        }
        ed->quit = true;
        break;

    case 0x0B: // Ctrl+K — cut line
        ed->has_cut = true;
        memcpy(ed->cut_buf, ed->lines[ed->cur_row], (size_t)(ed->lens[ed->cur_row] + 1));
        ed->cut_len = ed->lens[ed->cur_row];
        ed_delete_line(ed, ed->cur_row);
        ed_clamp_cursor(ed);
        ed->needs_redraw = true;
        break;

    case 0x15: // Ctrl+U — paste
        if (ed->has_cut) {
            // Insert blank line and fill it with cut buffer
            if (ed->count < ED_MAX_LINES) {
                for (int r = ed->count; r > ed->cur_row; r--) {
                    memcpy(ed->lines[r], ed->lines[r-1], (size_t)(ed->lens[r-1]+1));
                    ed->lens[r] = ed->lens[r-1];
                }
                ed->count++;
                memcpy(ed->lines[ed->cur_row], ed->cut_buf, (size_t)(ed->cut_len + 1));
                ed->lens[ed->cur_row] = ed->cut_len;
                ed->modified = true;
            }
        }
        ed->needs_redraw = true;
        break;

    case '\n':  // Enter
        ed_split_line(ed, ed->cur_row, ed->cur_col);
        ed->cur_row++;
        ed->cur_col = 0;
        ed_clamp_cursor(ed);
        ed->needs_redraw = true;
        break;

    case '\b':  // Backspace
        if (ed->cur_col > 0) {
            ed->cur_col--;
            ed_delete_char(ed, ed->cur_row, ed->cur_col);
        } else if (ed->cur_row > 0) {
            int prev_len = ed->lens[ed->cur_row - 1];
            ed_join_next(ed, ed->cur_row - 1);
            ed->cur_row--;
            ed->cur_col = prev_len;
        }
        ed_clamp_cursor(ed);
        ed->needs_redraw = true;
        break;

    default:
        if ((unsigned char)c >= 32 && (unsigned char)c < 127) {
            ed_insert_char(ed, ed->cur_row, ed->cur_col, c);
            ed->cur_col++;
            ed_clamp_cursor(ed);
            ed->needs_redraw = true;
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// Vim key handling
// ---------------------------------------------------------------------------

static void ed_vim_exec_cmd(editor_t* ed) {
    const char* cmd = ed->cmdline;
    bool wrote = false;
    bool quit_after = false;
    bool force = false;

    if (cmd[0] == 'w' && cmd[1] == 0) {
        wrote = ed_save(ed);
    } else if (cmd[0] == 'w' && cmd[1] == 'q' && cmd[2] == 0) {
        wrote = ed_save(ed); quit_after = true;
    } else if (cmd[0] == 'x' && cmd[1] == 0) {
        if (ed->modified) wrote = ed_save(ed);
        quit_after = true;
    } else if (cmd[0] == 'q' && cmd[1] == '!' && cmd[2] == 0) {
        force = true; quit_after = true;
    } else if (cmd[0] == 'q' && cmd[1] == 0) {
        if (!ed->modified) quit_after = true;
        // else: warn but stay (shown on status bar next redraw)
    }
    (void)wrote; (void)force;
    if (quit_after) ed->quit = true;
}

static void ed_vim_key(char c, void* user) {
    editor_t* ed = (editor_t*)user;
    if (!ed) return;

    // Clear any pending status message on keypress
    if (ed->msg[0]) { ed->msg[0] = 0; ed->msg_is_err = false; ed->needs_redraw = true; }

    // ESC sequences (arrow keys)
    if (ed->esc_state > 0 || (c == '\x1B' && ed->mode != ED_MODE_VIM_INSERT)) {
        if (c == '\x1B' && ed->esc_state == 0) { ed->esc_state = 1; return; }
        if (ed->esc_state > 0) {
            if (ed_parse_esc(ed, c)) return;
        }
    }

    switch (ed->mode) {

    // ---- Normal mode ----
    case ED_MODE_VIM_NORMAL:
        if (c == '\x1B') { ed->last_normal_key = 0; ed->needs_redraw = true; break; }
        if (c == 'i') { ed->mode = ED_MODE_VIM_INSERT; ed->last_normal_key = 0; ed->needs_redraw = true; break; }
        if (c == 'a') {
            if (ed->cur_col < ed->lens[ed->cur_row]) ed->cur_col++;
            ed->mode = ED_MODE_VIM_INSERT;
            ed->last_normal_key = 0;
            ed->needs_redraw = true;
            break;
        }
        if (c == 'A') {
            ed->cur_col = ed->lens[ed->cur_row];
            ed->mode = ED_MODE_VIM_INSERT;
            ed->last_normal_key = 0;
            ed->needs_redraw = true;
            break;
        }
        if (c == 'o') {
            // Open new line below
            ed_split_line(ed, ed->cur_row, ed->lens[ed->cur_row]);
            ed->cur_row++;
            ed->cur_col = 0;
            ed->mode = ED_MODE_VIM_INSERT;
            ed->last_normal_key = 0;
            ed->needs_redraw = true;
            break;
        }
        if (c == 'O') {
            // Open new line above
            ed_split_line(ed, ed->cur_row, 0);
            ed->cur_col = 0;
            ed->mode = ED_MODE_VIM_INSERT;
            ed->last_normal_key = 0;
            ed->needs_redraw = true;
            break;
        }
        if (c == ':') {
            ed->mode = ED_MODE_VIM_CMD;
            ed->cmdline[0] = 0;
            ed->cmdline_len = 0;
            ed->last_normal_key = 0;
            ed->needs_redraw = true;
            break;
        }
        // Movement
        if (c == 'h') { ed->cur_col--; ed_clamp_cursor(ed); ed->needs_redraw = true; break; }
        if (c == 'l') { ed->cur_col++; ed_clamp_cursor(ed); ed->needs_redraw = true; break; }
        if (c == 'j') { ed->cur_row++; ed_clamp_cursor(ed); ed->needs_redraw = true; break; }
        if (c == 'k') { ed->cur_row--; ed_clamp_cursor(ed); ed->needs_redraw = true; break; }
        if (c == '0') { ed->cur_col = 0; ed->needs_redraw = true; break; }
        if (c == '$') { ed->cur_col = ed->lens[ed->cur_row]; ed_clamp_cursor(ed); ed->needs_redraw = true; break; }
        if (c == 'G') { ed->cur_row = ed->count - 1; ed_clamp_cursor(ed); ed->needs_redraw = true; break; }
        // Two-char commands
        if (c == 'g' && ed->last_normal_key == 'g') {
            ed->cur_row = 0; ed->cur_col = 0; ed->last_normal_key = 0;
            ed->needs_redraw = true; break;
        }
        if (c == 'd' && ed->last_normal_key == 'd') {
            ed_delete_line(ed, ed->cur_row);
            ed_clamp_cursor(ed);
            ed->last_normal_key = 0;
            ed->needs_redraw = true; break;
        }
        if (c == 'x') {
            if (ed->cur_col < ed->lens[ed->cur_row])
                ed_delete_char(ed, ed->cur_row, ed->cur_col);
            ed_clamp_cursor(ed);
            ed->needs_redraw = true; break;
        }
        ed->last_normal_key = c;
        break;

    // ---- Insert mode ----
    case ED_MODE_VIM_INSERT:
        if (c == '\x1B') {
            ed->mode = ED_MODE_VIM_NORMAL;
            // Move cursor one left (vim convention)
            if (ed->cur_col > 0) ed->cur_col--;
            ed_clamp_cursor(ed);
            ed->needs_redraw = true;
            break;
        }
        if (c == '\n') {
            ed_split_line(ed, ed->cur_row, ed->cur_col);
            ed->cur_row++; ed->cur_col = 0;
            ed->needs_redraw = true; break;
        }
        if (c == '\b') {
            if (ed->cur_col > 0) {
                ed->cur_col--;
                ed_delete_char(ed, ed->cur_row, ed->cur_col);
            } else if (ed->cur_row > 0) {
                int prev_len = ed->lens[ed->cur_row - 1];
                ed_join_next(ed, ed->cur_row - 1);
                ed->cur_row--;
                ed->cur_col = prev_len;
            }
            ed_clamp_cursor(ed);
            ed->needs_redraw = true; break;
        }
        if ((unsigned char)c >= 32 && (unsigned char)c < 127) {
            ed_insert_char(ed, ed->cur_row, ed->cur_col, c);
            ed->cur_col++;
            ed->needs_redraw = true; break;
        }
        break;

    // ---- Command mode ----
    case ED_MODE_VIM_CMD:
        if (c == '\x1B') {
            ed->mode = ED_MODE_VIM_NORMAL;
            ed->needs_redraw = true; break;
        }
        if (c == '\n') {
            ed_vim_exec_cmd(ed);
            ed->mode = ED_MODE_VIM_NORMAL;
            ed->needs_redraw = true; break;
        }
        if (c == '\b') {
            if (ed->cmdline_len > 0) {
                ed->cmdline_len--;
                ed->cmdline[ed->cmdline_len] = 0;
            } else {
                ed->mode = ED_MODE_VIM_NORMAL;
            }
            ed->needs_redraw = true; break;
        }
        if ((unsigned char)c >= 32 && (unsigned char)c < 127 &&
            ed->cmdline_len < (int)(sizeof(ed->cmdline) - 1)) {
            ed->cmdline[ed->cmdline_len++] = c;
            ed->cmdline[ed->cmdline_len]   = 0;
            ed->needs_redraw = true; break;
        }
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Main editor entry points
// ---------------------------------------------------------------------------

static void ed_run(editor_t* ed) {
    ed->needs_redraw = true;
    ed->quit         = false;

    void (*key_fn)(char, void*) = (ed->mode == ED_MODE_NANO)
                                  ? ed_nano_key : ed_vim_key;

    shell_set_input_handler(key_fn, ed);

    while (!ed->quit) {
        __asm__ volatile("sti");
        if (ed->needs_redraw) {
            ed->needs_redraw = false;
            ed_draw(ed);
        }
        __asm__ volatile("hlt");
    }

    shell_set_input_handler(0, 0);

    // Restore normal terminal view: clear and reprint prompt
    ed_clear();
}

void editor_open_nano(const char* filename) {
    if (!filename || !filename[0]) { sh_printf("nano: no filename\n"); return; }

    editor_t* ed = (editor_t*)kmalloc(sizeof(editor_t));
    if (!ed) { sh_printf("nano: out of memory\n"); return; }
    memset(ed, 0, sizeof(editor_t));

    shell_get_term_size(&ed->term_rows, &ed->term_cols);
    if (ed->term_rows < 4) ed->term_rows = 24;
    if (ed->term_cols < 10) ed->term_cols = 80;

    int flen = (int)strlen(filename);
    if (flen >= (int)sizeof(ed->filename)) flen = (int)sizeof(ed->filename) - 1;
    memcpy(ed->filename, filename, (size_t)flen);
    ed->filename[flen] = 0;

    ed->mode  = ED_MODE_NANO;
    ed->count = 1;
    ed->lines[0][0] = 0;
    ed->lens[0] = 0;

    ed_load(ed, filename);

    g_ed = ed;
    ed_run(ed);
    g_ed = 0;

    kfree(ed);
    sh_printf("\n");
}

void editor_open_vim(const char* filename) {
    if (!filename || !filename[0]) { sh_printf("vim: no filename\n"); return; }

    editor_t* ed = (editor_t*)kmalloc(sizeof(editor_t));
    if (!ed) { sh_printf("vim: out of memory\n"); return; }
    memset(ed, 0, sizeof(editor_t));

    shell_get_term_size(&ed->term_rows, &ed->term_cols);
    if (ed->term_rows < 4) ed->term_rows = 24;
    if (ed->term_cols < 10) ed->term_cols = 80;

    int flen = (int)strlen(filename);
    if (flen >= (int)sizeof(ed->filename)) flen = (int)sizeof(ed->filename) - 1;
    memcpy(ed->filename, filename, (size_t)flen);
    ed->filename[flen] = 0;

    ed->mode  = ED_MODE_VIM_NORMAL;
    ed->count = 1;
    ed->lines[0][0] = 0;
    ed->lens[0] = 0;

    ed_load(ed, filename);

    g_ed = ed;
    ed_run(ed);
    g_ed = 0;

    kfree(ed);
    sh_printf("\n");
}
