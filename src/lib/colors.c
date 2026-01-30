#include "colors.h"
#include "string.h"
#include "utils.h"

// Helper: Case insensitive char compare (for hex)
static char to_upper(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

static uint8_t hex_char_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = to_upper(c);
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static uint32_t parse_hex_string(const char* hex) {
    // Skip optional prefix
    if (hex[0] == '#') hex++;
    else if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex += 2;

    size_t len = strlen(hex);
    uint32_t val = 0;

    // Parse bytes
    for (size_t i = 0; i < len && i < 8; i++) {
        val = (val << 4) | hex_char_to_int(hex[i]);
    }

    // Determine format
    if (len == 6) { 
        // Format: RRGGBB -> Convert to 0xFFRRGGBB
        return 0xFF000000 | val; 
    }
    
    // Format: AARRGGBB (or anything else, straightforward return)
    return val;
}

uint32_t color_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

uint32_t color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

uint32_t color_blend(uint32_t c1, uint32_t c2, int t) {
    // t is 0-255 representing interpolation factor.
    uint8_t a1 = (c1 >> 24) & 0xFF;
    uint8_t r1 = (c1 >> 16) & 0xFF;
    uint8_t g1 = (c1 >> 8) & 0xFF;
    uint8_t b1 = c1 & 0xFF;

    uint8_t a2 = (c2 >> 24) & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF;
    uint8_t g2 = (c2 >> 8) & 0xFF;
    uint8_t b2 = c2 & 0xFF;

    uint8_t a = lerp(a1, a2, t, 255);
    uint8_t r = lerp(r1, r2, t, 255);
    uint8_t g = lerp(g1, g2, t, 255);
    uint8_t b = lerp(b1, b2, t, 255);

    return color_rgba(r, g, b, a);
}

uint32_t color_parse(const char* str) {
    // Check for hex
    if (str[0] == '#' || (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))) {
        return parse_hex_string(str);
    }
    
    // Check if it's a naked hex like "FF00FF" (must be all hex chars)
    bool is_hex = true;
    for(int i=0; str[i]; i++) {
        char c = to_upper(str[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
            is_hex = false;
            break;
        }
    }
    if (is_hex && strlen(str) >= 6) return parse_hex_string(str);

    // Named Colors (Simple list)
    if (strcmp(str, "white") == 0) return COLOR_WHITE;
    if (strcmp(str, "black") == 0) return COLOR_BLACK;
    if (strcmp(str, "red") == 0) return COLOR_RED;
    if (strcmp(str, "green") == 0) return COLOR_GREEN; // Usually implied lime in web, but standard green here
    if (strcmp(str, "blue") == 0) return COLOR_BLUE;
    if (strcmp(str, "yellow") == 0) return COLOR_YELLOW;
    if (strcmp(str, "cyan") == 0) return COLOR_CYAN;
    if (strcmp(str, "magenta") == 0) return COLOR_MAGENTA;
    if (strcmp(str, "silver") == 0) return COLOR_SILVER;
    if (strcmp(str, "gray") == 0) return COLOR_GRAY;
    if (strcmp(str, "grey") == 0) return COLOR_GRAY;
    if (strcmp(str, "maroon") == 0) return COLOR_MAROON;
    if (strcmp(str, "olive") == 0) return COLOR_OLIVE;
    if (strcmp(str, "lime") == 0) return COLOR_LIME;
    if (strcmp(str, "purple") == 0) return COLOR_PURPLE;
    if (strcmp(str, "teal") == 0) return COLOR_TEAL;
    if (strcmp(str, "navy") == 0) return COLOR_NAVY;
    if (strcmp(str, "orange") == 0) return COLOR_ORANGE;
    if (strcmp(str, "transparent") == 0) return COLOR_TRANSPARENT;

    return 0; // Default black
}

uint8_t color_get_a(uint32_t color) { return (color >> 24) & 0xFF; }
uint8_t color_get_r(uint32_t color) { return (color >> 16) & 0xFF; }
uint8_t color_get_g(uint32_t color) { return (color >> 8) & 0xFF; }
uint8_t color_get_b(uint32_t color) { return color & 0xFF; }
