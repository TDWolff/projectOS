#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "../include/types.h"

// Line primitives
void graphics_draw_line(int x0, int y0, int x1, int y1, uint32_t color);

// Rectangle primitives
void graphics_draw_rect(int x, int y, int w, int h, uint32_t color);
void graphics_fill_rect(int x, int y, int w, int h, uint32_t color, bool border, uint32_t border_color, bool glass);
void graphics_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha, bool border, uint32_t border_color, bool glass);

// Circle primitives
void graphics_draw_circle(int x0, int y0, int radius, uint32_t color);
void graphics_fill_circle(int x0, int y0, int radius, uint32_t color, bool border, uint32_t border_color, bool glass);

// Rounded Rectangle primitives
void graphics_fill_round_rect_alpha(int x, int y, int w, int h, int radius, uint32_t color, uint8_t alpha, bool border, uint32_t border_color, bool glass);

#include "../lib/colors.h" // Expose colors to anyone including graphics.h
#include "../lib/utils.h"  // Expose utils to anyone including graphics.h

// --- Extended Graphics Library ---

typedef struct {
    int x, y, w, h;
} rect_t;

typedef struct {
    int x, y;
} point_t;

// State Management
void graphics_set_clipping_rect(int x, int y, int w, int h);
void graphics_clear_clipping_rect();
rect_t graphics_get_clipping_rect();

// Pixel access with clipping
void graphics_putpixel(int x, int y, uint32_t color);

// Triangle primitives
void graphics_draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color);
void graphics_fill_triangle(int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color);

// Gradient primitives
// Vertical: Top=c1 -> Bottom=c2. Horizontal: Left=c1 -> Right=c2
void graphics_fill_gradient_rect(int x, int y, int w, int h, uint32_t c1, uint32_t c2, bool vertical);

// Curve primitives
void graphics_draw_bezier_quad(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color);
void graphics_draw_bezier_cubic(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color);

// Polygon primitives
void graphics_draw_polygon(point_t* points, int count, uint32_t color);
void graphics_fill_polygon(point_t* points, int count, uint32_t color);

#endif
