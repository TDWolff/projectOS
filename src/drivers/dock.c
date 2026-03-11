#include "dock.h"

#include "graphics.h"
#include "vga.h"
#include "mouse.h"
#include "window.h"
#include "terminal_window.h"
#include "keyboard.h"
#include "shell.h"
#include "../lib/settings.h"
#include "../fs/initrd.h"
#include "../lib/string.h"
#include "../mem/heap.h"
#include "../lib/ico.h"

// Dock layout should match System UI / window clamp constants.
#define DOCK_HEIGHT 55
#define DOCK_BOTTOM_MARGIN 15
#define DOCK_RADIUS 16

#define DOCK_MAX_APPS 8
#define DOCK_ICON_SIZE 48
#define DOCK_ICON_PAD 10

static dock_app_t* g_dock_head = 0;
static int g_dock_count = 0;
static bool g_was_pressed = false;

// Cache dock rect for hit testing
static int g_dock_x = 0;
static int g_dock_y = 0;
static int g_dock_w = 0;
static int g_dock_h = 0;

// Manifest-driven dock entries:
// If a module in initrd ends with ".dock", we parse it as a tiny key=value file:
//   title=My App
//   app=stress_test.pexe
//   icon=myapp.ico
// Apps can ship a matching .dock file alongside the .pexe and .ico in `osstorage/`.

static bool str_ends_with(const char* s, const char* suffix) {
    if (!s || !suffix) return false;
    int sl = (int)strlen(s);
    int tl = (int)strlen(suffix);
    if (tl <= 0 || sl < tl) return false;
    return strcmp(s + (sl - tl), suffix) == 0;
}

static void trim_in_place(char* s) {
    if (!s) return;
    // left trim
    int i = 0;
    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') i++;
    if (i) {
        int j = 0;
        while (s[i]) s[j++] = s[i++];
        s[j] = 0;
    }
    // right trim
    int len = (int)strlen(s);
    while (len > 0) {
        char c = s[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            s[len - 1] = 0;
            len--;
        } else {
            break;
        }
    }
}

static void dock_register_from_manifest(const char* manifest_name, const void* data, uint64_t size) {
    (void)manifest_name;
    if (!data || size == 0) return;

    // We only need small manifests; cap for safety.
    if (size > 4096) size = 4096;

    char* buf = (char*)kmalloc(size + 1);
    if (!buf) return;
    memcpy(buf, data, (size_t)size);
    buf[size] = 0;

    char title[32];
    char app[64];
    char icon[64];
    bool background = false;
    memset(title, 0, sizeof(title));
    memset(app, 0, sizeof(app));
    memset(icon, 0, sizeof(icon));

    // Parse lines
    char* p = buf;
    while (*p) {
        // locate end of line
        char* line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        trim_in_place(line);
        if (line[0] == 0) continue;
        if (line[0] == '#') continue;

        // find '='
        int eq = 0;
        while (line[eq] && line[eq] != '=') eq++;
        if (line[eq] != '=') continue;

        line[eq] = 0;
        char* key = line;
        char* val = line + eq + 1;
        trim_in_place(key);
        trim_in_place(val);

        if (strcmp(key, "title") == 0) {
            int i = 0;
            for (; i < 31 && val[i]; i++) title[i] = val[i];
            title[i] = 0;
        } else if (strcmp(key, "app") == 0 || strcmp(key, "app_path") == 0) {
            int i = 0;
            for (; i < 63 && val[i]; i++) app[i] = val[i];
            app[i] = 0;
        } else if (strcmp(key, "icon") == 0 || strcmp(key, "icon_path") == 0) {
            int i = 0;
            for (; i < 63 && val[i]; i++) icon[i] = val[i];
            icon[i] = 0;
        } else if (strcmp(key, "background") == 0 || strcmp(key, "bg") == 0) {
            // Accept: 1/0, true/false
            if (val[0] == '1' || val[0] == 't' || val[0] == 'T' || val[0] == 'y' || val[0] == 'Y') background = true;
        }
    }

    if (app[0] != 0) {
        // icon is optional
        if (dock_add_app(title[0] ? title : 0, app, icon[0] ? icon : 0)) {
            // Optional: set background flag on the most recently added entry.
            // (We keep this hacky on purpose to avoid changing the public dock API right now.)
            if (background) {
                dock_app_t* last = g_dock_head;
                if (last) {
                    while (last->next) last = last->next;
                    last->background = true;
                }
            }
        }
    }

    kfree(buf);
}

static void dock_autodiscover_from_initrd() {
    file_t* files = initrd_get_files();
    if (!files) return;

    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].exists) continue;
        if (!str_ends_with(files[i].name, ".dock")) continue;
        dock_register_from_manifest(files[i].name, (const void*)files[i].address, (uint64_t)files[i].size);
    }
}

static bool streq(const char* a, const char* b) {
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

static void dock_compute_rect() {
    uint32_t screen_width = get_fb_width();
    uint32_t screen_height = get_fb_height();

    // Keep same math as the original `systemui.c` dock.
    uint32_t dock_width = (screen_width * 90) / 100;
    if (dock_width < 600) dock_width = 600;
    if (dock_width > screen_width - 40) dock_width = screen_width - 40;

    g_dock_w = (int)dock_width;
    g_dock_h = DOCK_HEIGHT;
    g_dock_x = (int)(screen_width - dock_width) / 2;
    g_dock_y = (int)screen_height - DOCK_HEIGHT - DOCK_BOTTOM_MARGIN;
}

static void dock_draw_background() {
    dock_compute_rect();

    // Use the same settings as the old SystemUI dock.
    uint32_t dock_color = 0xFFD0D0D0;
    int dock_alpha = 120;

    // Settings are optional; if missing it returns 0.
    extern int settings_get_int(const char* key);
    extern const char* settings_get(const char* key);
    
    uint32_t cfg_color = (uint32_t)settings_get_int("dock_color");
    int cfg_alpha = settings_get_int("dock_alpha");
    if (cfg_color != 0 || settings_get("dock_color") != 0) dock_color = cfg_color;
    if (cfg_alpha > 0) dock_alpha = cfg_alpha;

    graphics_fill_round_rect_alpha(
        g_dock_x,
        g_dock_y,
        g_dock_w,
        g_dock_h,
        DOCK_RADIUS,
        dock_color,
        (uint8_t)dock_alpha,
        false,
        0,
        true
    );
}

static void dock_free_icon(dock_app_t* app) {
    if (!app) return;
    if (app->icon_rgba) {
        kfree(app->icon_rgba);
        app->icon_rgba = 0;
    }
    app->icon_w = 0;
    app->icon_h = 0;
}

static void dock_try_load_icon(dock_app_t* app) {
    if (!app) return;
    if (app->icon_rgba) return; // already loaded
    if (app->icon_path[0] == 0) return;

    // Currently backed by initrd modules. Your build copies osstorage files into initrd.
    file_t* f = initrd_open(app->icon_path);
    if (!f) return;

    ico_image_t img;
    if (!ico_decode_best_fit((const void*)f->address, f->size, 128, &img)) {
        return;
    }

    app->icon_rgba = img.pixels;
    app->icon_w = img.width;
    app->icon_h = img.height;
}

static void dock_draw_icon_scaled(int dst_x, int dst_y, int dst_w, int dst_h, const dock_app_t* app) {
    if (!app || !app->icon_rgba || app->icon_w <= 0 || app->icon_h <= 0) {
        // placeholder
        graphics_fill_round_rect_alpha(dst_x, dst_y, dst_w, dst_h, 10, 0xFF2C2C2C, 180, false, 0, false);
        return;
    }

    // Nearest-neighbor scaling.
    for (int y = 0; y < dst_h; y++) {
        int sy = (y * app->icon_h) / dst_h;
        for (int x = 0; x < dst_w; x++) {
            int sx = (x * app->icon_w) / dst_w;
            uint32_t px = app->icon_rgba[sy * app->icon_w + sx];
            uint8_t a = (uint8_t)((px >> 24) & 0xFF);
            if (a == 0) continue;
            putpixel_alpha(dst_x + x, dst_y + y, px);
        }
    }
}

static void dock_launch_app(const dock_app_t* app) {
    if (!app) return;

    // If the dock entry is the Terminal launcher, open the kernel terminal window.
    // This is the most reliable path right now (no userland ABI/input routing needed).
    if (streq(app->app_path, "terminal.pexe") || streq(app->title, "Terminal")) {
        terminal_window_t* term = terminal_window_create(240, 160, 640, 420, "Terminal");
        if (!term) return;
        if (term->win) window_focus(term->win);
        shell_set_output_sink(terminal_window_shell_putc, term);
    shell_print_prompt();
        return;
    }

    // Background apps: run via shell without creating a visible terminal window.
    if (app->background) {
        // Detach shell output from any UI so we don't spam an existing terminal.
        shell_set_output_sink(0, 0);

        const char* prefix = "run ";
        for (int i = 0; prefix[i]; i++) shell_update(prefix[i]);
        for (int i = 0; app->app_path[i]; i++) shell_update(app->app_path[i]);
        shell_update('\n');
        return;
    }

    // For now: dock launch means "open a windowed terminal and type 'run <app_path>'".
    // This gives you end-to-end behavior today while keeping the data model right.
    // Later we can directly call the same loader as shell's run command.

    terminal_window_t* term = terminal_window_create(240, 160, 640, 420, app->title[0] ? app->title : "App");
    if (!term) return;

    // Focus the newly created window.
    if (term->win) window_focus(term->win);

    // Ensure shell output goes somewhere visible.
    shell_set_output_sink(terminal_window_shell_putc, term);

    // Type and execute: run <path>\n
    const char* prefix = "run ";
    for (int i = 0; prefix[i]; i++) terminal_window_input(term, prefix[i]);
    for (int i = 0; app->app_path[i]; i++) terminal_window_input(term, app->app_path[i]);
    terminal_window_input(term, '\n');

    // Also feed the shell parser so it actually runs.
    for (int i = 0; prefix[i]; i++) shell_update(prefix[i]);
    for (int i = 0; app->app_path[i]; i++) shell_update(app->app_path[i]);
    shell_update('\n');
}

void dock_init() {
    dock_clear();

    // First: allow apps to self-register by shipping a `.dock` manifest in initrd.
    // This keeps the kernel from needing per-app code changes.
    dock_autodiscover_from_initrd();

    // Fallback: if nothing registered, keep one example pinned so the UI isn't empty.
    if (g_dock_count == 0) {
        dock_add_app("Stress", "stress_test.pexe", "stress.ico");
    }
}

void dock_clear() {
    dock_app_t* cur = g_dock_head;
    while (cur) {
        dock_app_t* next = cur->next;
        dock_free_icon(cur);
        kfree(cur);
        cur = next;
    }
    g_dock_head = 0;
    g_dock_count = 0;
}

bool dock_add_app(const char* title, const char* app_path, const char* icon_path) {
    if (!app_path || app_path[0] == 0) return false;
    if (g_dock_count >= DOCK_MAX_APPS) return false;

    // Don’t add duplicates by path.
    for (dock_app_t* it = g_dock_head; it; it = it->next) {
        if (streq(it->app_path, app_path)) return false;
    }

    dock_app_t* app = (dock_app_t*)kmalloc(sizeof(dock_app_t));
    if (!app) return false;
    memset(app, 0, sizeof(dock_app_t));

    // Copy strings
    if (title) {
        int i = 0;
        for (; i < 31 && title[i]; i++) app->title[i] = title[i];
        app->title[i] = 0;
    }

    {
        int i = 0;
        for (; i < 63 && app_path[i]; i++) app->app_path[i] = app_path[i];
        app->app_path[i] = 0;
    }

    if (icon_path) {
        int i = 0;
        for (; i < 63 && icon_path[i]; i++) app->icon_path[i] = icon_path[i];
        app->icon_path[i] = 0;
    }

    // append to list
    app->next = 0;
    if (!g_dock_head) {
        g_dock_head = app;
    } else {
        dock_app_t* tail = g_dock_head;
        while (tail->next) tail = tail->next;
        tail->next = app;
    }

    g_dock_count++;
    return true;
}

bool dock_remove_app(const char* app_path) {
    if (!app_path) return false;

    dock_app_t* prev = 0;
    dock_app_t* cur = g_dock_head;
    while (cur) {
        if (streq(cur->app_path, app_path)) {
            if (prev) prev->next = cur->next;
            else g_dock_head = cur->next;
            dock_free_icon(cur);
            kfree(cur);
            g_dock_count--;
            return true;
        }
        prev = cur;
        cur = cur->next;
    }
    return false;
}

void dock_update(int mouse_x, int mouse_y, bool mouse_pressed_left) {
    dock_draw_background();

    // layout icons horizontally centered within dock
    int icon = DOCK_ICON_SIZE;
    int count = g_dock_count;
    if (count <= 0) return;

    int total_w = count * icon + (count - 1) * DOCK_ICON_PAD;
    int start_x = g_dock_x + (g_dock_w - total_w) / 2;
    int y = g_dock_y + (g_dock_h - icon) / 2;

    bool just_pressed = mouse_pressed_left && !g_was_pressed;

    dock_app_t* cur = g_dock_head;
    for (int i = 0; cur; i++, cur = cur->next) {
        dock_try_load_icon(cur);

        int x = start_x + i * (icon + DOCK_ICON_PAD);

        // hover highlight
        bool hover = (mouse_x >= x && mouse_x < x + icon && mouse_y >= y && mouse_y < y + icon);
        if (hover) {
            graphics_fill_round_rect_alpha(x - 2, y - 2, icon + 4, icon + 4, 12, 0xFF000000, 30, false, 0, false);
        }

        dock_draw_icon_scaled(x, y, icon, icon, cur);

        if (hover && just_pressed) {
            dock_launch_app(cur);
        }
    }

    g_was_pressed = mouse_pressed_left;
}
