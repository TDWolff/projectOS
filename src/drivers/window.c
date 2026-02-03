#include "window.h"
#include "graphics.h"
#include "vga.h"
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
#define BTN_YELLOW 0xFFFFBD2E
#define BTN_GREEN  0xFF27C93F

// Global Window List
static window_t* window_list_head = 0;
static window_t* window_list_tail = 0;

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

window_t* window_create(int x, int y, int width, int height, const char* title) {
    window_t* win = (window_t*)kmalloc(sizeof(window_t));
    if (!win) return 0;

    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    
    // Copy title safely
    int i;
    for (i = 0; i < 31 && title[i]; i++) {
        win->title[i] = title[i];
    }
    win->title[i] = 0;

    win->next = 0;
    win->flags = 0;

    // Auto-register window
    window_register(win);

    return win;
}

static window_t* dragging_window = 0;
static int drag_offset_x = 0;
static int drag_offset_y = 0;
static bool was_mouse_pressed = false;

void window_handle_mouse(int mouse_x, int mouse_y, uint8_t buttons) {
    bool is_pressed = (buttons & 1); // Left click

    // 1. Mouse Just Pressed: Check for title bar clicks
    if (is_pressed && !was_mouse_pressed) {
        window_t* current = window_list_head;
        // Iterate to find the top-most window under cursor? 
        // Our list is currently Back-to-Front (Head is drawn first, Tail last).
        // So we should iterate to find the *last* window that contains the click.
        
        window_t* hit_win = 0;
        
        while (current) {
            // Check Hitbox (Whole Window for now, refined to Title Bar)
            if (mouse_x >= current->x && mouse_x < current->x + current->width &&
                mouse_y >= current->y && mouse_y < current->y + current->height) {
                
                // Specific Check: Title Bar Only (Top 28px)
                if (mouse_y < current->y + WIN_TITLE_HEIGHT) {
                    hit_win = current;
                }
            }
            current = current->next;
        }

        if (hit_win) {
            dragging_window = hit_win;
            drag_offset_x = mouse_x - hit_win->x;
            drag_offset_y = mouse_y - hit_win->y;
            
            // TODO: Move hit_win to end of list (focus it)
        }
    }

    // 2. Mouse Dragging: Update Position
    if (is_pressed && dragging_window) {
        dragging_window->x = mouse_x - drag_offset_x;
        dragging_window->y = mouse_y - drag_offset_y;

        // Get Screen Dimensions
        int screen_w = (int)get_fb_width();
        int screen_h = (int)get_fb_height();

        // Clamp Top (Top Bar is 28px) - Keep existing logic
        if (dragging_window->y < 28) {
            dragging_window->y = 28;
        }

        // Clamp Left
        if (dragging_window->x < 0) {
            dragging_window->x = 0;
        }

        // Clamp Right
        if (dragging_window->x + dragging_window->width > screen_w) {
            dragging_window->x = screen_w - dragging_window->width;
        }

        // Clamp Bottom
        if (dragging_window->y + dragging_window->height > screen_h) {
            dragging_window->y = screen_h - dragging_window->height;
        }
    }

    // 3. Mouse Released: Stop Dragging
    if (!is_pressed) {
        dragging_window = 0;
    }

    was_mouse_pressed = is_pressed;
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
    
    // Minimize (Yellow)
    graphics_fill_circle(btn_start_x + btn_spacing, btn_y, BTN_RADIUS, BTN_YELLOW, false, 0, false);
    
    // Zoom (Green)
    graphics_fill_circle(btn_start_x + (btn_spacing * 2), btn_y, BTN_RADIUS, BTN_GREEN, false, 0, false);

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
}
