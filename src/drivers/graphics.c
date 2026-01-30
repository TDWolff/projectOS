#include "graphics.h"
#include "vga.h"
#include "../mem/heap.h"
#include "../lib/string.h"
#include "../lib/colors.h" // Added Colors Library
#include "../lib/utils.h"  // Added Utils Library

// Bresenham's Line Algorithm
void graphics_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        putpixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// Draw a hollow rectangle
void graphics_draw_rect(int x, int y, int w, int h, uint32_t color) {
    graphics_draw_line(x, y, x + w - 1, y, color);             // Top
    graphics_draw_line(x, y + h - 1, x + w - 1, y + h - 1, color); // Bottom
    graphics_draw_line(x, y, x, y + h - 1, color);             // Left
    graphics_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color); // Right
}

// Helper to "blur" a rectangle region in the framebuffer
// This uses a Stack Blur approximation or Box Blur, but optimized to run over a region.
// To achieve "Glass" (Apple style), we need to:
// 1. Copy the current screen background where the rect WILL be.
// 2. Blur that copy.
// 3. Draw that blurred copy back to the screen.
// 4. Draw the semi-transparent color ON TOP.

static void graphics_blur_area(int x, int y, int w, int h, int mask_radius) {
    // 1. Validate Bounds
    int screen_w = (int)get_fb_width();
    int screen_h = (int)get_fb_height();
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > screen_w) w = screen_w - x;
    if (y + h > screen_h) h = screen_h - y;
    if (w <= 0 || h <= 0) return;

    // 2. Allocate buffer for the region
    uint32_t* buffer = (uint32_t*)kmalloc(w * h * sizeof(uint32_t));
    if (!buffer) return;

    // 3. Read current background
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            buffer[i * w + j] = getpixel(x + j, y + i);
        }
    }

    // 4. Apply Blur (Simple Box Blur Pass)
    uint32_t* temp_buffer = (uint32_t*)kmalloc(w * h * sizeof(uint32_t));
    if (!temp_buffer) { kfree(buffer); return; }

    int blur_r = 3; // Kernel radius (Higher = more blur, slower)

    // Horizontal Pass
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            uint32_t r = 0, g = 0, b = 0;
            int count = 0;
            
            for (int k = -blur_r; k <= blur_r; k++) {
                int px = j + k;
                if (px >= 0 && px < w) {
                    uint32_t c = buffer[i * w + px];
                    r += (c >> 16) & 0xFF;
                    g += (c >> 8) & 0xFF;
                    b += (c) & 0xFF;
                    count++;
                }
            }
            temp_buffer[i * w + j] = ((r / count) << 16) | ((g / count) << 8) | (b / count);
        }
    }
    
    // Vertical Pass (from temp back to buffer)
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            uint32_t r = 0, g = 0, b = 0;
            int count = 0;
            
            for (int k = -blur_r; k <= blur_r; k++) {
                int py = i + k;
                if (py >= 0 && py < h) {
                    uint32_t c = temp_buffer[py * w + j];
                    r += (c >> 16) & 0xFF;
                    g += (c >> 8) & 0xFF;
                    b += (c) & 0xFF;
                    count++;
                }
            }
            buffer[i * w + j] = ((r / count) << 16) | ((g / count) << 8) | (b / count);
        }
    }

    // 5. Draw Blurred Background back (WITH MASKING for Rounded Corners)
    int r_sq = mask_radius * mask_radius;

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            bool draw = true;
            
            // Apply Rounded Mask if radius > 0
            if (mask_radius > 0) {
                // Check 4 corners
                // Top-Left
                if (i < mask_radius && j < mask_radius) {
                    int dy = mask_radius - 1 - i;
                    int dx = mask_radius - 1 - j;
                    if (dx*dx + dy*dy > r_sq) draw = false;
                }
                // Top-Right
                else if (i < mask_radius && j >= w - mask_radius) {
                    int dy = mask_radius - 1 - i;
                    int dx = j - (w - mask_radius);
                    if (dx*dx + dy*dy > r_sq) draw = false;
                }
                // Bottom-Left
                else if (i >= h - mask_radius && j < mask_radius) {
                    int dy = i - (h - mask_radius);
                    int dx = mask_radius - 1 - j;
                    if (dx*dx + dy*dy > r_sq) draw = false;
                }
                // Bottom-Right
                else if (i >= h - mask_radius && j >= w - mask_radius) {
                    int dy = i - (h - mask_radius);
                    int dx = j - (w - mask_radius);
                    if (dx*dx + dy*dy > r_sq) draw = false;
                }
            }

            if (draw) {
                putpixel(x + j, y + i, buffer[i * w + j]);
            }
        }
    }

    kfree(buffer);
    kfree(temp_buffer);
}

// Draw a filled rectangle (solid color)
void graphics_fill_rect(int x, int y, int w, int h, uint32_t color, bool border, uint32_t border_color, bool glass) {
    if (glass) {
        graphics_blur_area(x, y, w, h, 0); // Radius 0 for rectangle
    }

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            // if (glass) apply_blur... NO, we already blurred the background.
            // Now we just compose the color on top.
            // If it's a solid fill (no alpha), the blur gets covered anyway?
            // "graphics_fill_rect" implies solid color. Glass implies transparency.
            // If alpha is hidden in color (AARRGGBB), putpixel might ignore it if it's not putpixel_alpha.
            // But this function uses putpixel. 
            // So if glass is true, we probably shouldn't draw FULLY OPAQUE over it, or else the blur is useless.
            // Assuming this function is for solid colors, glass here is meaningless unless we treat color as alpha capable?
            // Let's assume standard behavior: solid cover. 
            // If user wants glass, they should use alpha function.
            putpixel(x + j, y + i, color);
        }
    }
    
    if (border) {
        graphics_draw_rect(x, y, w, h, border_color);
    }
}

// Draw a filled rectangle with alpha transparency
// Alpha: 0 = Invisible, 255 = Opaque
void graphics_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha, bool border, uint32_t border_color, bool glass) {
    // 1. Apply Glass Effect to the background region FIRST
    if (glass) {
        graphics_blur_area(x, y, w, h, 0); // Radius 0 for rectangle
    }
    
    // Mask out any existing alpha and apply the new one
    uint32_t final_color = (color & 0x00FFFFFF) | ((uint32_t)alpha << 24);

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            // Note: Background is already blurred. We just blend on top.
            putpixel_alpha(x + j, y + i, final_color);
        }
    }

    if (border) {
        graphics_draw_rect(x, y, w, h, border_color);
    }
}

// Draw a hollow circle using Bresenham's circle algorithm
void graphics_draw_circle(int x0, int y0, int radius, uint32_t color) {
    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y) {
        putpixel(x0 + x, y0 + y, color);
        putpixel(x0 + y, y0 + x, color);
        putpixel(x0 - y, y0 + x, color);
        putpixel(x0 - x, y0 + y, color);
        putpixel(x0 - x, y0 - y, color);
        putpixel(x0 - y, y0 - x, color);
        putpixel(x0 + y, y0 - x, color);
        putpixel(x0 + x, y0 - y, color);

        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }
}

// Draw a filled circle
void graphics_fill_circle(int x0, int y0, int radius, uint32_t color, bool border, uint32_t border_color, bool glass) {
    // For circles, blurring a square area bounding the circle is easiest/fastest,
    // though it blurs corners that aren't touched. That's acceptable for now?
    // Or we could try to verify pixel-by-pixel.
    // Glass usually implies a "material".
    
    // Optimization: Blur the bounding box.
    if (glass) {
        graphics_blur_area(x0 - radius, y0 - radius, radius * 2, radius * 2, radius);
    }

    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y) {
        // Draw horizontal lines between the edges
        // Upper half: y0 - y, from x0 - x to x0 + x
        // Lower half: y0 + y, from x0 - x to x0 + x
        // Inner Upper: y0 - x, from x0 - y to x0 + y
        // Inner Lower: y0 + x, from x0 - y to x0 + y

        // We need loops to apply blur to every pixel... NO LONGER NEEDED.
        for (int k = x0 - x; k <= x0 + x; k++) {
             // if (glass) ... removed
             putpixel(k, y0 + y, color);
             putpixel(k, y0 - y, color);
        }
        
        for (int k = x0 - y; k <= x0 + y; k++) {
             // if (glass) ... removed
             putpixel(k, y0 + x, color);
             putpixel(k, y0 - x, color);
        }

        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }

    if (border) {
        graphics_draw_circle(x0, y0, radius, border_color);
    }
}

// Draw a filled rounded rectangle with alpha transparency
void graphics_fill_round_rect_alpha(int x, int y, int w, int h, int radius, uint32_t color, uint8_t alpha, bool border, uint32_t border_color, bool glass) {
    // 1. Blur the general area first
    if (glass) {
        graphics_blur_area(x, y, w, h, radius);
        // Note: This blurs the corners (outside the round rect) too.
        // A perfect solution would only blur pixels inside the mask.
        // But for a generic OS UI, blurring the bounding box is usually "good enough" 
        // if the background isn't wildly changing at the exact corner pixels.
        // If it looks bad, we can mask the blur, but that's expensive.
    }

    // 1. Clamp radius
    if (radius < 0) radius = 0;
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;

    uint32_t final_color = (color & 0x00FFFFFF) | ((uint32_t)alpha << 24);

    // We can't use simple overlapping rectangles for alpha blending because overlaps become darker.
    // Solution: Draw unique horizontal scanlines.

    int r_sq = radius * radius;
    
    // 1. Top Section (y to y + radius - 1)
    for (int i = 0; i < radius; i++) {
        // int dy = radius - 1 - i; // REMOVED unused variable
        
        int center_dist = radius - i; 
        int dx = 0;
        
        // Find dx such that dx^2 + center_dist^2 <= r_sq
        // Simple linear search or logic (since radius is small)
        // Optimization: Start from radius and go down? Or start 0 and go up?
        // dx will be roughly sqrt(r^2 - center_dist^2).
        while ((dx + 1) * (dx + 1) + center_dist * center_dist <= r_sq) {
            dx++;
        }
        
        // Draw Scanline
        // Left bound: x + radius - dx
        // Right bound: x + w - 1 - radius + dx
        // Note: The "corners" are centered at (x+radius) and (x+w-1-radius).
        
        int start_x = x + radius - dx;
        int end_x = x + w - 1 - radius + dx;
        
        for (int col = start_x; col <= end_x; col++) {
             // if (glass) apply_blur(col, y + i); // REMOVED
             putpixel_alpha(col, y + i, final_color);
        }
    }

    // 2. Middle Section (Solid Rect)
    // Rows from (y + radius) to (y + h - 1 - radius)
    for (int i = radius; i < h - radius; i++) {
        for (int col = x; col < x + w; col++) {
             // if (glass) apply_blur(col, y + i); // REMOVED
             putpixel_alpha(col, y + i, final_color);
        }
    }

    // 3. Bottom Section
    // Rows from (y + h - radius) to (y + h - 1)
    // Symmetrical to top
    for (int i = 0; i < radius; i++) {
        int center_dist = i + 1; // distance from bottom corner centers (y + h - 1 - radius)
                                 // row is (y + h - radius + i).
                                 // center y is (y + h - 1 - radius). 
                                 // wait, center is y + h - radius - 1? No.
                                 // center is at (y + h - 1 - radius)? 
                                 // If h=20, r=10. bottom starts y+10. ends y+19.
                                 // bottom center is y+10? No, usually y + h - r.
                                 // If we assume standard rect logic:
                                 // Top center y = y + r.
                                 // Bottom center y = y + h - r - 1. (Adjusted for 0-indexing)
                                 
        // Let's just mirror the dy logic.
        // center_dist = i + 1; Is correct if we iterate i from 0 away from center.
        // dx calculation is valid.

        int dx = 0;
        while ((dx + 1) * (dx + 1) + center_dist * center_dist <= r_sq) {
            dx++;
        }
        
        int start_x = x + radius - dx;
        int end_x = x + w - 1 - radius + dx;
        int row_y = y + h - radius + i;

        for (int col = start_x; col <= end_x; col++) {
             // if (glass) apply_blur(col, row_y); // REMOVED
             putpixel_alpha(col, row_y, final_color);
        }
    }
    
    // 4. Optional Border (Drawn on top)
    if (border) {
        // Draw flat sides
        graphics_draw_line(x + radius, y, x + w - 1 - radius, y, border_color); // Top
        graphics_draw_line(x + radius, y + h - 1, x + w - 1 - radius, y + h - 1, border_color); // Bottom
        graphics_draw_line(x, y + radius, x, y + h - 1 - radius, border_color); // Left
        graphics_draw_line(x + w - 1, y + radius, x + w - 1, y + h - 1 - radius, border_color); // Right
        
        // Draw Arcs using Bresenham explicitly for corners
        int cx = radius;
        int cy = 0;
        int err = 0;
        
        // Centers
        int tl_xc = x + radius;
        int tl_yc = y + radius;
        int tr_xc = x + w - 1 - radius;
        int tr_yc = y + radius;
        int bl_xc = x + radius;
        int bl_yc = y + h - 1 - radius;
        int br_xc = x + w - 1 - radius;
        int br_yc = y + h - 1 - radius;

        while (cx >= cy) {
            // Plot 8 octants -> 4 corners
            
            // TL
            putpixel(tl_xc - cx, tl_yc - cy, border_color);
            putpixel(tl_xc - cy, tl_yc - cx, border_color);
            
            // TR
            putpixel(tr_xc + cx, tr_yc - cy, border_color);
            putpixel(tr_xc + cy, tr_yc - cx, border_color);
            
            // BL
            putpixel(bl_xc - cx, bl_yc + cy, border_color);
            putpixel(bl_xc - cy, bl_yc + cx, border_color);
            
            // BR
            putpixel(br_xc + cx, br_yc + cy, border_color);
            putpixel(br_xc + cy, br_yc + cx, border_color);

            if (err <= 0) {
                cy += 1;
                err += 2 * cy + 1;
            }
            if (err > 0) {
                cx -= 1;
                err -= 2 * cx + 1;
            }
        }
    }
}

// ---------------------------------------------------------
//        Extended Graphics Library Implementation
// ---------------------------------------------------------

static bool clipping_enabled = false;
static rect_t clip_rect = {0, 0, 0, 0};

void graphics_set_clipping_rect(int x, int y, int w, int h) {
    clip_rect.x = x;
    clip_rect.y = y;
    clip_rect.w = w;
    clip_rect.h = h;
    clipping_enabled = true;
}

void graphics_clear_clipping_rect() {
    clipping_enabled = false;
}

rect_t graphics_get_clipping_rect() {
    if (!clipping_enabled) {
        rect_t r = {0, 0, (int)get_fb_width(), (int)get_fb_height()};
        return r;
    }
    return clip_rect;
}

// Wrapper for putpixel that respects clipping 
void graphics_putpixel(int x, int y, uint32_t color) {
    if (clipping_enabled) {
        if (x < clip_rect.x || x >= clip_rect.x + clip_rect.w ||
            y < clip_rect.y || y >= clip_rect.y + clip_rect.h) {
            return;
        }
    }
    putpixel(x, y, color);
}

// Helper: Swap two integers
static void swap(int* a, int* b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

// Draw a hollow triangle
void graphics_draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color) {
    graphics_draw_line(x1, y1, x2, y2, color);
    graphics_draw_line(x2, y2, x3, y3, color);
    graphics_draw_line(x3, y3, x1, y1, color);
}

// Fill a triangle (Standard Scanline Algorithm)
void graphics_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
    // Sort vertices by Y (y0 <= y1 <= y2)
    if (y0 > y1) { swap(&x0, &x1); swap(&y0, &y1); }
    if (y0 > y2) { swap(&x0, &x2); swap(&y0, &y2); }
    if (y1 > y2) { swap(&x1, &x2); swap(&y1, &y2); }

    int total_height = y2 - y0;
    for (int i = 0; i < total_height; i++) {
        bool second_half = i > y1 - y0 || y1 == y0;
        int segment_height = second_half ? y2 - y1 : y1 - y0;
        
        // Be careful not to divide by zero
        if (segment_height == 0) segment_height = 1;

        // Use integer math for interpolation ratios
        // alpha = i / total_height
        // beta  = (i - (second_half ? y1 - y0 : 0)) / segment_height
        
        // We use lerp with precision of 1000 for ratios
        int alpha_num = i;
        int alpha_den = total_height;

        int beta_num = i - (second_half ? y1 - y0 : 0);
        int beta_den = segment_height;

        int A = x0 + ((x2 - x0) * (long long)alpha_num) / alpha_den;
        int B = second_half ? 
                x1 + ((x2 - x1) * (long long)beta_num) / beta_den : 
                x0 + ((x1 - x0) * (long long)beta_num) / beta_den; 

        if (A > B) { int temp = A; A = B; B = temp; }

        for (int j = A; j <= B; j++) {
            putpixel(j, y0 + i, color);
        }
    }
}

// Quadratic Bezier Curve (3 control points)
void graphics_draw_bezier_quad(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
    int prev_x = x0;
    int prev_y = y0;
    int steps = 20;

    for (int i = 1; i <= steps; i++) {
        // Use Integer lerp
        int xa = lerp(x0, x1, i, steps);
        int ya = lerp(y0, y1, i, steps);
        int xb = lerp(x1, x2, i, steps);
        int yb = lerp(y1, y2, i, steps);
        
        int x = lerp(xa, xb, i, steps);
        int y = lerp(ya, yb, i, steps);

        graphics_draw_line(prev_x, prev_y, x, y, color);
        prev_x = x;
        prev_y = y;
    }
}

// Cubic Bezier Curve (4 control points)
void graphics_draw_bezier_cubic(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color) {
    int prev_x = x0;
    int prev_y = y0;
    int steps = 20;

    for (int i = 1; i <= steps; i++) {
        // int xa = lerp(x0, x1, t);
        // int xb = lerp(x1, x2, t);
        // int xc = lerp(x2, x3, t);
        // int xm = lerp(xa, xb, t);
        // int xn = lerp(xb, xc, t);
        // int x = lerp(xm, xn, t);
        
        int xa = lerp(x0, x1, i, steps);
        int ya = lerp(y0, y1, i, steps);
        int xb = lerp(x1, x2, i, steps);
        int yb = lerp(y1, y2, i, steps);
        int xc = lerp(x2, x3, i, steps);
        int yc = lerp(y2, y3, i, steps);
        
        int xm = lerp(xa, xb, i, steps);
        int ym = lerp(ya, yb, i, steps);
        int xn = lerp(xb, xc, i, steps);
        int yn = lerp(yb, yc, i, steps);
        
        int x = lerp(xm, xn, i, steps);
        int y = lerp(ym, yn, i, steps);

        graphics_draw_line(prev_x, prev_y, x, y, color);
        prev_x = x;
        prev_y = y;
    }
}

// Draw Polygon (connected lines)
void graphics_draw_polygon(point_t* points, int count, uint32_t color) {
    if (count < 2) return;
    for (int i = 0; i < count - 1; i++) {
        graphics_draw_line(points[i].x, points[i].y, points[i+1].x, points[i+1].y, color);
    }
    // Close loop
    graphics_draw_line(points[count-1].x, points[count-1].y, points[0].x, points[0].y, color);
}

// Fill Polygon (Scanline algorithm)
void graphics_fill_polygon(point_t* points, int count, uint32_t color) {
     if (count < 3) return;

     // Find bounding box Y-range
     int min_y = points[0].y;
     int max_y = points[0].y;
     for (int i = 1; i < count; i++) {
         if (points[i].y < min_y) min_y = points[i].y;
         if (points[i].y > max_y) max_y = points[i].y;
     }

     // Use simple static array for nodes to avoid malloc if possible, 
     // or just scan bounding box + winding (slow but simple) or strict scanline.
     // For simplicity in kernel mode without malloc:
     
     for (int y = min_y; y <= max_y; y++) {
         // Find intersections
         int nodes[32]; // Max 32 intersections per line
         int node_count = 0;
         int j = count - 1;
         
         for (int i = 0; i < count; i++) {
             if ((points[i].y < y && points[j].y >= y) || 
                 (points[j].y < y && points[i].y >= y)) {
                 
                 // Calculate X intersection
                 nodes[node_count++] = points[i].x + (y - points[i].y) * (points[j].x - points[i].x) / (points[j].y - points[i].y);
                 if (node_count >= 32) break;
             }
             j = i;
         }

         // Sort nodes (Bubble sort is fine for small N)
         for (int i = 0; i < node_count - 1; i++) {
             for (int k = 0; k < node_count - 1 - i; k++) {
                 if (nodes[k] > nodes[k+1]) {
                     swap(&nodes[k], &nodes[k+1]);
                 }
             }
         }

         // Fill pairs
         for (int i = 0; i < node_count; i += 2) {
             if (i + 1 >= node_count) break;
             for (int x = nodes[i]; x <= nodes[i+1]; x++) {
                 graphics_putpixel(x, y, color);
             }
         }
     }
}

// Fill Rectangle with Gradient
void graphics_fill_gradient_rect(int x, int y, int w, int h, uint32_t c1, uint32_t c2, bool vertical) {
    // Extract colors once
    int r1 = (c1 >> 16) & 0xFF;
    int g1 = (c1 >> 8) & 0xFF;
    int b1 = c1 & 0xFF;

    int r2 = (c2 >> 16) & 0xFF;
    int g2 = (c2 >> 8) & 0xFF;
    int b2 = c2 & 0xFF;

    if (vertical) {
        for (int i = 0; i < h; i++) {
            // Lerp each component
            // current step is i, total steps is h
            uint8_t r = (uint8_t)lerp(r1, r2, i, h);
            uint8_t g = (uint8_t)lerp(g1, g2, i, h);
            uint8_t b = (uint8_t)lerp(b1, b2, i, h);
            
            uint32_t color = (r << 16) | (g << 8) | b;
            graphics_draw_line(x, y + i, x + w - 1, y + i, color);
        }
    } else {
        for (int i = 0; i < w; i++) {
            // Lerp each component
            // current step is i, total steps is w
            uint8_t r = (uint8_t)lerp(r1, r2, i, w);
            uint8_t g = (uint8_t)lerp(g1, g2, i, w);
            uint8_t b = (uint8_t)lerp(b1, b2, i, w);

            uint32_t color = (r << 16) | (g << 8) | b;
            graphics_draw_line(x + i, y, x + i, y + h - 1, color);
        }
    }
}
