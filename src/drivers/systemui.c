#include "systemui.h"
#include "vga.h"
#include "graphics.h"
#include "../lib/colors.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../lib/float.h" // For future clock/animations
#include "../lib/settings.h"
#include "rtc.h" // Added RTC
#include "../mem/heap.h" // Cache allocation
#include "dock.h"

// --- Configuration ---

// Top Bar
#define TOPBAR_HEIGHT 28
#define TOPBAR_COLOR "black" // 0xFF202020
#define TOPBAR_ALPHA 180     // Semi-transparent

// Dock (Formerly Taskbar)
#define DOCK_HEIGHT 55
#define DOCK_BOTTOM_MARGIN 15 
#define DOCK_COLOR "white"
#define DOCK_ALPHA 140      
#define DOCK_RADIUS 16

// Cache for the clean topbar (Glass + Text, but no clock)
static uint32_t* topbar_backing_store = 0;

// --- Private Helpers ---

static void refresh_clock() {
    if (!settings_get_int("show_clock")) return;
    if (!topbar_backing_store) return; // Can't refresh if no background cache

    uint32_t screen_width = get_fb_width();

    // 1. Get Time
    rtc_time_t t = rtc_get_time();
        
    // Apply Timezone
    int tz = settings_get_int("timezone");
    int hour = t.hours + tz;

    // Handle overflow/underflow
    if (hour < 0) hour += 24;
    if (hour >= 24) hour -= 24;
    
    // 12h Format Logic
    char* ampm = "AM";
    if (hour >= 12) {
        ampm = "PM";
        if (hour > 12) hour -= 12;
    }
    if (hour == 0) hour = 12;

    char time_str[32];
    char buf[4];
    int offset = 0;
    
    // Format String
    itoa(hour, buf, 10);
    int len = strlen(buf);
    memcpy(time_str + offset, buf, len);
    offset += len;
    
    time_str[offset++] = ':';
    
    if (t.minutes < 10) time_str[offset++] = '0';
    itoa(t.minutes, buf, 10);
    len = strlen(buf);
    memcpy(time_str + offset, buf, len);
    offset += len;
    
    if (settings_get_int("show_seconds")) {
        time_str[offset++] = ':';
        if (t.seconds < 10) time_str[offset++] = '0';
        itoa(t.seconds, buf, 10);
        len = strlen(buf);
        memcpy(time_str + offset, buf, len);
        offset += len;
    }
    
    time_str[offset++] = ' ';
    len = strlen(ampm);
    memcpy(time_str + offset, ampm, len);
    offset += len;
    time_str[offset] = 0;

    int time_width = strlen(time_str) * 8;
    
    // 2. Position Logic
    int clock_x;
    const char* pos_str = settings_get("clock_pos");
    
    if (pos_str && strcmp(pos_str, "right") == 0) {
        clock_x = screen_width - time_width - 15;
    } else {
        clock_x = (screen_width - time_width) / 2;
    }

    // 3. RESTORE BACKGROUND (The "Separate Layer" effect)
    // We clear a slightly larger area to ensure no artifacts when digits change width (e.g. 1 vs 2)
    int clear_x = clock_x - 10;
    int clear_w = time_width + 20;
    
    // Bounds check
    if (clear_x < 0) clear_x = 0;
    if (clear_x + clear_w > (int)screen_width) clear_w = screen_width - clear_x;

    for (int y = 0; y < TOPBAR_HEIGHT; y++) {
        for (int x = 0; x < clear_w; x++) {
            int screen_x = clear_x + x;
            // Restore pixel from cache
            putpixel(screen_x, y, topbar_backing_store[y * screen_width + screen_x]);
        }
    }

    // 4. Draw New Time
    video_draw_text(clock_x, 8, time_str, 0xFF000000);
}

static void draw_top_bar() {
    uint32_t screen_width = get_fb_width();
    int alpha = settings_get_int("topbar_alpha");
    if (alpha <= 0) alpha = 180;
    
    // Get Color from Settings
    const char* color_str = settings_get("topbar_color");
    uint32_t color = 0xFF000000; // Default Black
    if (color_str) {
        color = color_parse(color_str);
    }

    // 1. Draw Background (Dark Glass)
    graphics_fill_rect_alpha(
        0, 0, 
        screen_width, TOPBAR_HEIGHT, 
        color, 
        (uint8_t)alpha, 
        false, 0, true
    );

    // 2. Draw System Title (Left)
    const char* system = settings_get("system");
    video_draw_text(12, 8, system ? system : "ProjectOS", 0xFF000000); 

    // 3. CACHE the clean UI (Background + Title)
    // We do this BEFORE drawing the clock, so the cache represents the "empty" state under the clock.
    if (!topbar_backing_store) {
        topbar_backing_store = (uint32_t*)kmalloc(screen_width * TOPBAR_HEIGHT * 4);
    }
    
    // Copy current framebuffer to cache
    for (int y = 0; y < TOPBAR_HEIGHT; y++) {
        for (uint32_t x = 0; x < screen_width; x++) {
            topbar_backing_store[y * screen_width + x] = getpixel(x, y);
        }
    }

    // 4. Draw Initial Clock
    refresh_clock();
}

static void draw_dock() {
    // Background + icons are owned by dock.c now.
    dock_update(-1, -1, false);
}

// --- Public API ---

void systemui_init() {
    draw_top_bar();
    dock_init();
    draw_dock();
}

void systemui_update() {
    // Refresh only the clock part using the cached background
    refresh_clock();

    // Re-draw dock icons (and handle click updates from the main loop).
    // The main loop should call dock_update with real mouse coords.
}
