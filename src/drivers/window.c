#include "window.h"
#include "graphics.h"
#include "vga.h"
#include "terminal_window.h"
#include "mouse.h"
#include "../mem/heap.h"
#include "../lib/string.h"
#include "../lib/colors.h"

// macOS-like Window Constants
#define WIN_RADIUS 10
#define WIN_TITLE_HEIGHT 28
#define WIN_BG_COLOR     0xFFF8F8F8 // Almost white content
#define WIN_TITLE_COLOR  0xFFE0E0E0 // Light grey title bar
#define WIN_BORDER_COLOR 0xFFAAAAAA // Subtle border

// Traffic Light Constants
#define BTN_RADIUS 6
#define BTN_RED    0xFFFF5F56
#define BTN_GREEN  0xFF27C93F

// Window Flags
#define WIN_FLAG_MAXIMIZED (1u << 0)

// Desktop layout (should match System UI)
#define TOPBAR_HEIGHT 28
#define DOCK_HEIGHT 55
#define DOCK_BOTTOM_MARGIN 15

// Global Window List
static window_t* window_list_head = 0;
static window_t* window_list_tail = 0;

// The currently focused window (top-most active).
static window_t* g_focused_window = 0;

// Track last mouse button state for edge-triggered clicks
static bool was_mouse_pressed = false;

// "Wheel" emulation: Hold right mouse button and move mouse vertically to scroll.
static bool was_right_pressed = false;
static int scroll_anchor_y = 0;
static int scroll_accum_y = 0;

static void window_focus_internal(window_t* win);

static bool point_in_rect(int px, int py, int x, int y, int w, int h) {
    return (px >= x && px < x + w && py >= y && py < y + h);
}

window_t* window_get_focused() {
    return g_focused_window;
}

void window_set_char_input_handler(window_t* win, void (*on_char_input)(window_t* win, char c, void* user), void* user) {
    if (!win) return;
    win->on_char_input = on_char_input;
    win->on_char_input_user = user;
}

static bool point_in_circle(int px, int py, int cx, int cy, int r) {
    int dx = px - cx;
    int dy = py - cy;
    return (dx * dx + dy * dy) <= (r * r);
}

static void window_remove(window_t* win) {
    if (!win) return;

    window_t* prev = 0;
    window_t* current = window_list_head;

    while (current) {
        if (current == win) {
            // Unlink
            if (prev) {
                prev->next = current->next;
            } else {
                window_list_head = current->next;
            }

            if (window_list_tail == current) {
                window_list_tail = prev;
            }

            // Free
            kfree(current);
            return;
        }

        prev = current;
        current = current->next;
    }
}

void window_close(window_t* win) {
    if (!win) return;
    window_remove(win);
    if (g_focused_window == win) g_focused_window = 0;
    if (win->content_cache) { kfree(win->content_cache); win->content_cache = 0; }
}

void window_mark_dirty(window_t* win) {
    if (win) win->content_dirty = true;
}

static void window_register(window_t* win) {
    if (!win) return;
    
    // Add to linked list
    if (!window_list_head) {
        window_list_head = win;
        window_list_tail = win;
    } else {
        window_list_tail->next = win;
        window_list_tail = win;
    }
}

static void window_focus_internal(window_t* win) {
    if (!win) return;
    if (window_list_tail == win) return; // already on top

    // Unlink
    window_t* prev = 0;
    window_t* cur = window_list_head;
    while (cur) {
        if (cur == win) break;
        prev = cur;
        cur = cur->next;
    }
    if (!cur) return; // not in list

    if (prev) prev->next = cur->next;
    else window_list_head = cur->next;

    if (window_list_tail == cur) window_list_tail = prev;

    // Append to tail
    cur->next = 0;
    if (!window_list_head) {
        window_list_head = cur;
        window_list_tail = cur;
    } else {
        window_list_tail->next = cur;
        window_list_tail = cur;
    }
}

void window_focus(window_t* win) {
    window_focus_internal(win);
}

window_t* window_create(int x, int y, int width, int height, const char* title) {
    window_t* win = (window_t*)kmalloc(sizeof(window_t));
    if (!win) return 0;

    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    // New windows start focused by default.
    g_focused_window = win;
    
    // Copy title safely
    int i;
    for (i = 0; i < 31 && title[i]; i++) {
        win->title[i] = title[i];
    }
    win->title[i] = 0;

    win->next = 0;
    win->flags = 0;
    win->restore_x = x;
    win->restore_y = y;
    win->restore_w = width;
    win->restore_h = height;
    win->draw_content = 0;
    win->draw_content_user = 0;
    win->content_dirty = true;
    win->content_cache = 0;
    win->cache_cw = 0;
    win->cache_ch = 0;

    // Auto-register window
    window_register(win);

    return win;
}

void window_set_content_renderer(window_t* win, void (*draw_content)(window_t* win, void* user), void* user) {
    if (!win) return;
    win->draw_content = draw_content;
    win->draw_content_user = user;
}

void window_get_content_rect(window_t* win, int* out_x, int* out_y, int* out_w, int* out_h) {
    if (!win) return;
    if (out_x) *out_x = win->x;
    if (out_y) *out_y = win->y + WIN_TITLE_HEIGHT;
    if (out_w) *out_w = win->width;
    if (out_h) *out_h = win->height - WIN_TITLE_HEIGHT;
}

static window_t* dragging_window = 0;
static int drag_offset_x = 0;
static int drag_offset_y = 0;

static void window_toggle_maximize(window_t* win) {
    if (!win) return;

    int screen_w = (int)get_fb_width();
    int screen_h = (int)get_fb_height();

    int usable_top = TOPBAR_HEIGHT;
    int usable_bottom = screen_h - (DOCK_HEIGHT + DOCK_BOTTOM_MARGIN);

    if (usable_bottom < usable_top + 1) {
        usable_bottom = usable_top + 1;
    }

    if (win->flags & WIN_FLAG_MAXIMIZED) {
        // Restore
        win->flags &= ~WIN_FLAG_MAXIMIZED;
        win->x = win->restore_x;
        win->y = win->restore_y;
        win->width = win->restore_w;
        win->height = win->restore_h;
        return;
    }

    // Maximize
    win->flags |= WIN_FLAG_MAXIMIZED;
    win->restore_x = win->x;
    win->restore_y = win->y;
    win->restore_w = win->width;
    win->restore_h = win->height;

    win->x = 0;
    win->y = usable_top;
    win->width = screen_w;
    win->height = usable_bottom - usable_top;
}

void window_handle_mouse(int mouse_x, int mouse_y, uint8_t buttons) {
    bool is_pressed = (buttons & 1); // Left click
    bool is_right_pressed = (buttons & 2); // Right click

    // Mouse wheel -> terminal scroll (when focused window is a terminal)
    // Wheel deltas can arrive even when no buttons are pressed.
    int wheel = mouse_consume_wheel_delta();
    if (wheel != 0) {
        window_t* focused = window_get_focused();
        if (focused && focused->draw_content_user) {
            terminal_window_t* term = (terminal_window_t*)focused->draw_content_user;
            if (term && term->win == focused) {
                // Convention: wheel positive = up.
                terminal_window_scroll(term, wheel);
            }
        }
    }

    // 1. Mouse Just Pressed: Check for title bar clicks
    if (is_pressed && !was_mouse_pressed) {
        window_t* current = window_list_head;
        // Iterate to find the top-most window under cursor? 
        // Our list is currently Back-to-Front (Head is drawn first, Tail last).
        // So we should iterate to find the *last* window that contains the click.
        
        window_t* hit_win = 0;
        
        while (current) {
            // Check Hitbox (Whole Window for now, refined to Title Bar)
            if (point_in_rect(mouse_x, mouse_y, current->x, current->y, current->width, current->height)) {
                
                // Specific Check: Title Bar Only (Top 28px)
                if (mouse_y < current->y + WIN_TITLE_HEIGHT) {
                    hit_win = current;
                }
            }
            current = current->next;
        }

        if (hit_win) {
            // If the click is on the red close button, close the window.
            // Button layout must match `window_draw()`.
            int btn_start_x = hit_win->x + 18;
            int btn_y = hit_win->y + 16;
            int btn_spacing = 22;
            if (point_in_circle(mouse_x, mouse_y, btn_start_x, btn_y, BTN_RADIUS)) {
                // If we were dragging this window somehow, stop.
                if (dragging_window == hit_win) {
                    dragging_window = 0;
                }

                window_close(hit_win);
                was_mouse_pressed = is_pressed;
                return;
            }

            // Zoom (Green) - maximize/restore
            int green_x = btn_start_x + btn_spacing;
            if (point_in_circle(mouse_x, mouse_y, green_x, btn_y, BTN_RADIUS)) {
                // If we were dragging this window somehow, stop.
                if (dragging_window == hit_win) {
                    dragging_window = 0;
                }

                window_toggle_maximize(hit_win);
                was_mouse_pressed = is_pressed;
                return;
            }

            // Focus the window before starting drag so it comes to front.
            window_focus_internal(hit_win);

            dragging_window = hit_win;
            drag_offset_x = mouse_x - hit_win->x;
            drag_offset_y = mouse_y - hit_win->y;
        }
    }

    // 2. Mouse Dragging: Update Position
    if (is_pressed && dragging_window) {
        // Don't allow dragging a maximized window.
        if (dragging_window->flags & WIN_FLAG_MAXIMIZED) {
            was_mouse_pressed = is_pressed;
            return;
        }

        dragging_window->x = mouse_x - drag_offset_x;
        dragging_window->y = mouse_y - drag_offset_y;

        // Get Screen Dimensions
        int screen_w = (int)get_fb_width();
        int screen_h = (int)get_fb_height();

        // Clamp Top (Top Bar)
        if (dragging_window->y < TOPBAR_HEIGHT) {
            dragging_window->y = TOPBAR_HEIGHT;
        }

        // Clamp Left
        if (dragging_window->x < 0) {
            dragging_window->x = 0;
        }

        // Clamp Right
        if (dragging_window->x + dragging_window->width > screen_w) {
            dragging_window->x = screen_w - dragging_window->width;
        }

        // Clamp Bottom (stay above Dock area)
        int usable_bottom = screen_h - (DOCK_HEIGHT + DOCK_BOTTOM_MARGIN);
        if (dragging_window->y + dragging_window->height > usable_bottom) {
            dragging_window->y = usable_bottom - dragging_window->height;
        }
    }

    // 2b. Terminal scroll gesture (right-click + move up/down)
    // This avoids needing real PS/2 wheel packet decoding.
    if (is_right_pressed && !was_right_pressed) {
        scroll_anchor_y = mouse_y;
        scroll_accum_y = 0;
    }

    if (is_right_pressed) {
        window_t* focused = window_get_focused();
        if (focused && focused->draw_content_user) {
            // Heuristic: terminal windows store a `terminal_window_t*` as draw_content_user.
            terminal_window_t* term = (terminal_window_t*)focused->draw_content_user;
            if (term && term->win == focused) {
                int dy = mouse_y - scroll_anchor_y;
                scroll_anchor_y = mouse_y;
                scroll_accum_y += dy;

                // One line per ~half a character cell.
                int step = TERM_CHAR_H / 2;
                if (step < 1) step = 1;

                while (scroll_accum_y <= -step) {
                    terminal_window_scroll(term, +1);
                    scroll_accum_y += step;
                }
                while (scroll_accum_y >= step) {
                    terminal_window_scroll(term, -1);
                    scroll_accum_y -= step;
                }
            }
        }
    }

    // 3. Mouse Released: Stop Dragging
    if (!is_pressed) {
        dragging_window = 0;
    }

    was_mouse_pressed = is_pressed;
    was_right_pressed = is_right_pressed;
}

// Iterate and draw all windows
void window_paint_all() {
    window_t* current = window_list_head;
    while (current) {
        window_draw(current);
        current = current->next;
    }
}

void window_draw(window_t* win) {
    if (!win) return;
    
    // We construct the window using a layering technique to handle rounded corners
    // correctly for both the title bar (top) and content (bottom).

    // 1. Draw the main background (Content Color) - Provides bottom rounded corners
    graphics_fill_round_rect_alpha(
        win->x,
        win->y,
        win->width,
        win->height,
        WIN_RADIUS,
        WIN_BG_COLOR,
        255, // Opaque
        true,
        WIN_BORDER_COLOR,
        false
    );

    // 2. Draw the Title Bar "Cap" (Title Color) - Provides top rounded corners
    // We draw a rounded rect slightly deeper than the title height, then cover the bottom half
    graphics_fill_round_rect_alpha(
        win->x,
        win->y,
        win->width,
        WIN_TITLE_HEIGHT + WIN_RADIUS, // Extend down to ensure curvature is drawn
        WIN_RADIUS,
        WIN_TITLE_COLOR,
        255, 
        true,
        WIN_BORDER_COLOR,
        false
    );

    // 3. "Cut" the bottom of the Title Bar Cap to make it flat
    // We draw a rectangle of the CONTENT color over the bottom part of the cap
    // This connects the Title Bar (Top) to the Body (Bottom) seamlessly
    graphics_fill_rect_alpha(
        win->x + 1, // +1 to avoid overwriting the left border
        win->y + WIN_TITLE_HEIGHT,
        win->width - 2, // -2 to avoid right border
        WIN_RADIUS, // Height just needs to cover the curvature overlap
        WIN_BG_COLOR,
        255,
        false, 0, false
    );

    // 4. Draw the Separator Line
    graphics_draw_line(
        win->x, 
        win->y + WIN_TITLE_HEIGHT, 
        win->x + win->width, 
        win->y + WIN_TITLE_HEIGHT, 
        0xFFC0C0C0 // Separator color
    );

    // 5. Draw Traffic Lights (Top Left)
    int btn_start_x = win->x + 18;
    int btn_y = win->y + 16;
    int btn_spacing = 22;

    // Close (Red)
    graphics_fill_circle(btn_start_x, btn_y, BTN_RADIUS, BTN_RED, false, 0, false);
    
        // Draw X inside the red button
        // Keep it small so it stays within the circle.
        int x_size = 3;
        graphics_draw_line(btn_start_x - x_size, btn_y - x_size, btn_start_x + x_size, btn_y + x_size, 0xFF000000);
        graphics_draw_line(btn_start_x - x_size, btn_y + x_size, btn_start_x + x_size, btn_y - x_size, 0xFF000000);

    // Zoom (Green)
    graphics_fill_circle(btn_start_x + btn_spacing, btn_y, BTN_RADIUS, BTN_GREEN, false, 0, false);
    
        // Draw + inside the green button
        int green_x = btn_start_x + btn_spacing;
        int plus_size = 3;
        graphics_draw_line(green_x - plus_size, btn_y, green_x + plus_size, btn_y, 0xFF000000);
        graphics_draw_line(green_x, btn_y - plus_size, green_x, btn_y + plus_size, 0xFF000000);

    // 6. (Optional) Title
    // Simple text centering logic
    // We assume 8px width per char for the default font
    
    // Default system font is usually 8px wide.
    int text_len = strlen(win->title);
    int text_width = text_len * 8; 
    
    // Center X
    int title_x = win->x + (win->width - text_width) / 2;
    // Center Y (Approximate for 16px font height in 28px bar)
    int title_y = win->y + (WIN_TITLE_HEIGHT - 16) / 2;

    // Draw Window Title (Dark Grey Text)
    video_draw_text(title_x, title_y, win->title, 0xFF404040);

    // 7. Draw window content using the cache.
    // On dirty frames: call the renderer and save the result to the cache.
    // On clean frames: restore the cache directly — no font rendering, no fill.
    if (win->draw_content) {
        int cx, cy, cw, ch;
        window_get_content_rect(win, &cx, &cy, &cw, &ch);

        // Reallocate cache if size changed (first use or window resized).
        if (cw > 0 && ch > 0 && (win->cache_cw != cw || win->cache_ch != ch)) {
            if (win->content_cache) kfree(win->content_cache);
            win->content_cache = (uint32_t*)kmalloc((uint32_t)(cw * ch) * sizeof(uint32_t));
            win->cache_cw = cw;
            win->cache_ch = ch;
            win->content_dirty = true;
        }

        uint32_t* draw   = video_get_draw_target();
        uint32_t  stride = get_fb_pitch() / 4; // pixels per row in draw buffer

        if (win->content_dirty || !win->content_cache) {
            // Full render into the draw buffer.
            win->draw_content(win, win->draw_content_user);
            win->content_dirty = false;

            // Save content region into cache (one memcpy per row).
            if (win->content_cache && draw) {
                for (int row = 0; row < ch; row++) {
                    memcpy(&win->content_cache[row * cw],
                           &draw[(cy + row) * stride + cx],
                           (uint32_t)cw * sizeof(uint32_t));
                }
            }
        } else if (win->content_cache && draw) {
            // Restore cache to draw buffer — fast rectangular blit.
            for (int row = 0; row < ch; row++) {
                memcpy(&draw[(cy + row) * stride + cx],
                       &win->content_cache[row * cw],
                       (uint32_t)cw * sizeof(uint32_t));
            }
        }
    }
}
