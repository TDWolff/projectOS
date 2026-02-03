#include "systemui.h"
#include "vga.h"
#include "graphics.h"
#include "../lib/colors.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../lib/float.h" // For future clock/animations
#include "../lib/settings.h"
#include "rtc.h" // Added RTC

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

// --- Private Helpers ---

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
    const char* user = settings_get("system");
    video_draw_text(12, 8, user ? user : "ProjectOS", 0xFFFFFFFF);

    // 3. Draw Clock (Center) - Now Real!
    if (settings_get_int("show_clock")) {
        rtc_time_t t = rtc_get_time();
        
        // Apply Timezone
        int tz = settings_get_int("timezone");
        int hour = t.hours + tz;

        // Handle overflow/underflow
        if (hour < 0) hour += 24;
        if (hour >= 24) hour -= 24;
        
        // Format: HH:MM[:SS] AM/PM
        // Convert to 12h format
        char* ampm = "AM";
        
        if (hour >= 12) {
            ampm = "PM";
            if (hour > 12) hour -= 12;
        }
        if (hour == 0) hour = 12; // Midnight is 12 AM

        char time_str[32]; // enlarged to be safe if seconds are shown
        // Helper to convert int to string buffer
        char buf[4];
        
        int offset = 0;
        
        // Hour
        itoa(hour, buf, 10);
        int len = strlen(buf);
        memcpy(time_str + offset, buf, len);
        offset += len;
        
        time_str[offset++] = ':';
        
        // Minute (with leading zero)
        if (t.minutes < 10) {
            time_str[offset++] = '0';
        }
        itoa(t.minutes, buf, 10);
        len = strlen(buf);
        memcpy(time_str + offset, buf, len);
        offset += len;
        
        // Optional seconds
        if (settings_get_int("show_seconds")) {
            time_str[offset++] = ':';
            if (t.seconds < 10) {
                time_str[offset++] = '0';
            }
            itoa(t.seconds, buf, 10);
            len = strlen(buf);
            memcpy(time_str + offset, buf, len);
            offset += len;
        }
        
        time_str[offset++] = ' ';
        
        // AM/PM
        len = strlen(ampm);
        memcpy(time_str + offset, ampm, len);
        offset += len;
        
        time_str[offset] = 0; // Null terminate

        int time_width = strlen(time_str) * 8; // 8px per char
        video_draw_text((screen_width - time_width) / 2, 8, time_str, 0xFFFFFFFF);
    }
}

static void draw_dock() {
    uint32_t screen_width = get_fb_width();
    uint32_t screen_height = get_fb_height();

    // Load Settings
    uint32_t dock_color = settings_get_int("dock_color");
    int dock_alpha = settings_get_int("dock_alpha");
    if (dock_color == 0 && settings_get("dock_color") == 0) dock_color = 0xFFFFFF; // Default white
    if (dock_alpha <= 0) dock_alpha = 140;

    // Calculate width: 90% of screen, clamp min/max
    uint32_t dock_width = (screen_width * 90) / 100;
    if (dock_width < 600) dock_width = 600;
    if (dock_width > screen_width - 40) dock_width = screen_width - 40;

    // Center the dock
    int x = (screen_width - dock_width) / 2;
    int y = screen_height - DOCK_HEIGHT - DOCK_BOTTOM_MARGIN;

    // Draw the rounded dock (Glass Effect)
    graphics_fill_round_rect_alpha(
        x, y, 
        dock_width, DOCK_HEIGHT, 
        DOCK_RADIUS,
        dock_color, 
        (uint8_t)dock_alpha, 
        true, // Border
        0x40FFFFFF, // Subtle white border
        true // Glass Blur
    );
     
    // TODO: Draw Application Icons here
}

// --- Public API ---

void systemui_init() {
    draw_top_bar();
    draw_dock();
}

void systemui_update() {
    // Redraw just the top bar to update the clock
    draw_top_bar();
}
