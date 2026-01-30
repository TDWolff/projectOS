#ifndef COLORS_H
#define COLORS_H

#include "../include/types.h"

// ---------------------------------------------------------
//                Standard Colors
// ---------------------------------------------------------
// Format: 0xAARRGGBB (Alpha, Red, Green, Blue)

#define COLOR_TRANSPARENT 0x00000000
#define COLOR_BLACK       0xFF000000
#define COLOR_WHITE       0xFFFFFFFF

// Primary Colors
#define COLOR_RED         0xFFFF0000
#define COLOR_LIME        0xFF00FF00
#define COLOR_BLUE        0xFF0000FF

// Secondary / Web Colors
#define COLOR_YELLOW      0xFFFFFF00
#define COLOR_CYAN        0xFF00FFFF // Aqua
#define COLOR_MAGENTA     0xFFFF00FF // Fuchsia
#define COLOR_SILVER      0xFFC0C0C0
#define COLOR_GRAY        0xFF808080
#define COLOR_MAROON      0xFF800000
#define COLOR_OLIVE       0xFF808000
#define COLOR_GREEN       0xFF008000
#define COLOR_PURPLE      0xFF800080
#define COLOR_TEAL        0xFF008080
#define COLOR_NAVY        0xFF000080

#define COLOR_ORANGE      0xFFFFA500
#define COLOR_PINK        0xFFFFC0CB
#define COLOR_GOLD        0xFFFFD700
#define COLOR_TOMATO      0xFFFF6347
#define COLOR_SKYBLUE     0xFF87CEEB

// ---------------------------------------------------------
//                Color Functions
// ---------------------------------------------------------

// Construct a color from RGB (Alpha defaults to 255/FF)
uint32_t color_rgb(uint8_t r, uint8_t g, uint8_t b);

// Construct a color from RGBA
uint32_t color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

// Helper to blend two colors
// t = 0.0 (c1) to 1.0 (c2)
uint32_t color_blend(uint32_t c1, uint32_t c2, int t);

// Parse a string to a color
// Supported formats:
// - Hex: "#RRGGBB", "#AARRGGBB", "0xRRGGBB", "RRGGBB"
// - Names: "red", "blue", "teal", "white", etc.
uint32_t color_parse(const char* str);

// Extract components
uint8_t color_get_a(uint32_t color);
uint8_t color_get_r(uint32_t color);
uint8_t color_get_g(uint32_t color);
uint8_t color_get_b(uint32_t color);

#endif
