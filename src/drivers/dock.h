#ifndef DOCK_H
#define DOCK_H

#include "../include/types.h"

// Simple dock app entry. For now, dock entries are static launchers pointing
// at a .pexe path and an optional .ico path.
//
// Notes:
// - icon is optional; if missing or fails to load, a placeholder is drawn.
// - app_path is required.
// - max items is intentionally small for now (8).

typedef struct dock_app_t {
    char title[32];
    char app_path[64];
    char icon_path[64];

    // Launch behavior
    bool background;

    // runtime/icon cache
    uint32_t* icon_rgba; // ARGB or XRGB; matches putpixel format
    int icon_w;
    int icon_h;

    struct dock_app_t* next;
} dock_app_t;

void dock_init();

// Clears all entries.
void dock_clear();

// Add a launcher to the dock.
// Returns 0 on failure (full/out of memory), non-zero on success.
bool dock_add_app(const char* title, const char* app_path, const char* icon_path);

// Remove launcher by app_path. Returns true if removed.
bool dock_remove_app(const char* app_path);

// Called every frame to handle hover/click and to draw icons.
// `mouse_pressed_left` should be current left button state.
void dock_update(int mouse_x, int mouse_y, bool mouse_pressed_left);

#endif
