#include "taskbar.h"
#include "vga.h"
#include "graphics.h"

#define TASKBAR_HEIGHT 45
// Now we can use strings! ("silver", "#C0C0C0", "white", etc.)
#define TASKBAR_COLOR "white"
#define TASKBAR_ALPHA 130      // 255 * decimal = opacity percentage
#define TASKBAR_BOTTOM_MARGIN 10 
#define TASKBAR_RADIUS 10

void taskbar_init() {
    uint32_t screen_width = get_fb_width();
    uint32_t screen_height = get_fb_height();

    // Calculate width: 95% of screen, but at least 500px
    uint32_t taskbar_width = (screen_width * 95) / 100;
    if (taskbar_width < 500) taskbar_width = 500;
    
    // Safety clamp if screen is smaller than 500
    if (taskbar_width > screen_width) taskbar_width = screen_width;

    // Center the taskbar
    int x = (screen_width - taskbar_width) / 2;
    int y = screen_height - TASKBAR_HEIGHT - TASKBAR_BOTTOM_MARGIN;

    // Draw the rounded taskbar
    graphics_fill_round_rect_alpha(
        x, 
        y, 
        taskbar_width, 
        TASKBAR_HEIGHT, 
        TASKBAR_RADIUS,
        color_parse(TASKBAR_COLOR), // Automatically parse the defined string here
        TASKBAR_ALPHA, 
        false, // No border
        0,
        true // Glass effect
    );
}
