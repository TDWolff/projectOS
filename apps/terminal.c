#include "libapp.h"

// Redundant types removed as they are in libapp.h
// typedef unsigned long long uint64_t;
// typedef unsigned int uint32_t;
// typedef unsigned short uint16_t;
// typedef unsigned char uint8_t;
typedef _Bool bool;
#define true 1
#define false 0

// Forward declaration
void _start() __attribute__((section(".text.entry")));

// Standard string functions
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strlen(const char* str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

// Shell Variables
#define MAX_COMMAND_LEN 128
char command_buffer[MAX_COMMAND_LEN];
int buffer_idx = 0;

// Graphics Globals
static uint32_t* fb_buffer = 0;
static uint32_t fb_pitch = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;

// Font Globals
static uint8_t* font_buffer = 0;
static uint32_t font_width = 8;
static uint32_t font_height = 16;
static uint32_t font_bytes = 16;

// Terminal Grid State
// Assuming approx 80x25 or slightly larger to fit 600x400 window
// The inner window is about 590px wide (win_w - 10)
// 590 / 8 = 73.75 chars
// The inner window is about 365px tall (win_h - 35)
// 365 / 16 = 22.8 lines
// We'll set the grid dimensions to match the visible constraints so text doesn't overflow.
#define TERM_COLS 73
#define TERM_ROWS 22
static char term_grid[TERM_ROWS][TERM_COLS];
static int cursor_row = 0;
static int cursor_col = 0;

// Window State
static int win_x = 200;
static int win_y = 150;
static int win_w = 600;
static int win_h = 400;
static bool is_dragging = false;
static int drag_offset_x = 0;
static int drag_offset_y = 0;
static int last_mouse_btn = 0;

// Icon Data (0: Black, 1: Gray, 2: White, 3: Transparent)
static int icon_data[13][16] = {
    {3,3,1,1,1,1,1,1,1,1,1,1,1,1,3,3},
    {3,1,0,0,0,0,0,0,0,0,0,0,0,0,1,3},
    {1,0,2,2,0,0,0,0,0,0,0,0,0,0,0,1}, // "C"
    {1,0,2,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,2,2,0,0,2,0,2,0,0,0,0,0,0,1}, // ":\" 
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,2,2,2,2,2,2,2,2,0,0,0,0,0,1}, // Text lines
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,2,2,2,2,2,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,2,2,2,0,0,0,0,0,0,0,0,0,0,1}, 
    {3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3},
    {3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3}
};

// Forward Decl
void term_putc(char c);
void draw_desktop();
void draw_shell_window();
void refresh_terminal();

void kprint(const char* str) {
    if (!font_buffer) {
        // Fallback if font init failed (use kernel syscall)
        sys_kprintf(str); 
        return;
    }
    
    while (*str) {
        term_putc(*str++);
    }
}

void kprint_char(char c) {
    if (!font_buffer) { sys_kprintf(&c); return; } // Hacky fallback pointer logic
    term_putc(c);
}

// Simple itoa for numbers (needed for LS)
void itoa(unsigned long long n, char* str) {
    if (n == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }
    int i = 0;
    unsigned long long temp = n;
    while (temp != 0) {
        i++;
        temp /= 10;
    }
    str[i] = '\0';
    while (n != 0) {
        str[--i] = (n % 10) + '0';
        n /= 10;
    }
}

// Draw the Desktop background behind the window (naive redraw for moving)
// In a real compositor we would save the background.
// Here we just fill with teal, which matches the kernel background.
// Note: We need a forward declaration or move it above draw_rect calls in other contexts if needed
void draw_rect(int x, int y, int w, int h, uint32_t color);

// Forward Decl
void refresh_terminal();
void putpixel(int x, int y, uint32_t color);
void draw_rect(int x, int y, int w, int h, uint32_t color);
void term_putc(char c);
void draw_shell_window(); // Added forward declaration

void draw_char(int x, int y, char c, uint32_t fg) {
    if (!font_buffer) return;
    
    // Safety
    if (x + font_width >= fb_width || y + font_height >= fb_height) return;

    uint8_t* glyph = font_buffer + (c * font_bytes);
    
    for (uint32_t cy = 0; cy < font_height; cy++) {
        for (uint32_t cx = 0; cx < 8; cx++) {
            // Check pixel in glyph
            // Note: This logic assumes simplified standard font bitmap layout 
            // (1 byte per row for width <= 8). 
            // Adjust if PSF logic is complex, but this matches video.c for 8-wide font.
            if (glyph[cy] & (0x80 >> cx)) {
               putpixel(x + cx, y + cy, fg);
            }
        }
    }
}

void refresh_terminal() {
    // Redraw the black background for text area first
    draw_rect(win_x + 5, win_y + 30, win_w - 10, win_h - 35, 0x000000);

    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
             char ch = term_grid[r][c];
             if (ch != 0 && ch != ' ') {
                 int px = win_x + 5 + (c * font_width);
                 int py = win_y + 30 + (r * font_height);
                 draw_char(px, py, ch, 0xFFFFFF);
             }
        }
    }
}

// Internal print that updates grid and draws
void term_putc(char c) {
    // 1. Handle Control Chars
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            term_grid[cursor_row][cursor_col] = ' ';
            // Redraw this cell as black
            int px = win_x + 5 + (cursor_col * font_width);
            int py = win_y + 30 + (cursor_row * font_height);
            draw_rect(px, py, font_width, font_height, 0x000000);
        }
    } else {
        // 2. Write to Grid
        if (cursor_col < TERM_COLS && cursor_row < TERM_ROWS) {
            term_grid[cursor_row][cursor_col] = c;
            
            // 3. Draw immediately
            int px = win_x + 5 + (cursor_col * font_width);
            int py = win_y + 30 + (cursor_row * font_height);
            draw_char(px, py, c, 0xFFFFFF); // White text
            
            cursor_col++;
        }
    }

    // 4. Wrap / Scroll
    if (cursor_col >= TERM_COLS) {
        cursor_col = 0;
        cursor_row++;
    }
    
    if (cursor_row >= TERM_ROWS) {
        // Scroll Up
        // Move memory
        for (int r = 1; r < TERM_ROWS; r++) {
            for (int c = 0; c < TERM_COLS; c++) {
                term_grid[r-1][c] = term_grid[r][c];
            }
        }
        // Clear last row
        for (int c = 0; c < TERM_COLS; c++) term_grid[TERM_ROWS-1][c] = ' ';
        
        cursor_row = TERM_ROWS - 1;
        refresh_terminal(); // Full redraw to show scroll
    }
}

void clear_old_window(int x, int y, int w, int h) {
    draw_rect(x, y, w, h, 0x008080); 
}

// Check if string ends with .pexe
bool is_pexe(const char* filename) {
    int len = strlen(filename);
    if (len < 5) return false;
    return (filename[len-5] == '.' && 
            filename[len-4] == 'p' && 
            filename[len-3] == 'e' && 
            filename[len-2] == 'x' && 
            filename[len-1] == 'e');
}

void execute_command(char* input) {
    if (strcmp(input, "help") == 0) {
        kprint("\nls, cat <file>, run <file.pexe>, help, exit, clear, echo");
    }
    else if (strcmp(input, "exit") == 0) {
        sys_exit();
    }
    else if (strcmp(input, "ls") == 0) {
         file_t files[MAX_FILES];
         sys_list_files(files);
         kprint("\n--- User Space Filesystem ---\n");
         for (int i = 0; i < MAX_FILES; i++) {
             if (files[i].exists) {
                 kprint(files[i].name);
                 kprint(" (");
                 char size_buf[32];
                 itoa(files[i].size, size_buf);
                 kprint(size_buf);
                 kprint(" bytes)\n");
             }
         }
    }
    else if (input[0] == 'c' && input[1] == 'a' && input[2] == 't' && input[3] == ' ') {
        char* filename = input + 4; // Skip "cat "
        
        // We can't access initrd directly in user mode! We must use sys_read_file
        // file_t* f = initrd_open(filename);
        
        char buffer[1024]; // Small buffer for now
        int bytes = sys_read_file(filename, buffer, 1024);
        
        if (bytes >= 0) {
            kprint("\n");
            for(int i=0; i < bytes; i++) {
                kprint_char(buffer[i]);
            }
         } else {
             kprint("\nFile not found: ");
             kprint(filename);
         }
    }
    // New: RUN command
    else if (input[0] == 'r' && input[1] == 'u' && input[2] == 'n' && input[3] == ' ') {
        char* filename = input + 4;
        if (is_pexe(filename)) {
            kprint("\nLaunching ");
            kprint(filename);
            kprint("...\n");
            sys_run(filename);
            
            // Restore UI after child process exits
            draw_desktop();
            draw_shell_window();
            refresh_terminal();
        } else {
            kprint("\nError: Can only run .pexe files");
        }
    }
    else if (strcmp(input, "version") == 0) {
        kprint("\nProjectOS Terminal v1.0 (User Space Edition)");
    }
    else if (input[0] == 'e' && input[1] == 'c' && input[2] == 'h' && input[3] == 'o') {
         kprint("\n");
         kprint(input + 5);
    }
    else {
        kprint("\nUnknown command: ");
        kprint(input);
    }
    kprint("\nuser % ");
}

void init_graphics() {
    fb_info_t fb;
    sys_get_fb_info(&fb);
    if (fb.addr != 0) {
        fb_buffer = (uint32_t*)fb.addr;
        fb_width = fb.width;
        fb_height = fb.height;
        fb_pitch = fb.pitch;
        
        font_buffer = (uint8_t*)fb.font_addr;
        font_width = fb.font_width;
        font_height = fb.font_height;
        font_bytes = fb.font_bytes;
        
        // Init Grid
        for(int i=0; i<TERM_ROWS; i++) 
            for(int j=0; j<TERM_COLS; j++) 
                term_grid[i][j] = ' ';
    }
}

void putpixel(int x, int y, uint32_t color) {
    if (!fb_buffer || x < 0 || x >= (int)fb_width || y < 0 || y >= (int)fb_height) return;
    uint32_t* pixel = (uint32_t*)((uint8_t*)fb_buffer + (y * fb_pitch) + (x * 4));
    *pixel = color;
}

uint32_t getpixel(int x, int y) {
    if (!fb_buffer || x < 0 || x >= (int)fb_width || y < 0 || y >= (int)fb_height) return 0;
    return *(uint32_t*)((uint8_t*)fb_buffer + (y * fb_pitch) + (x * 4));
}

void rect_copy(int sx, int sy, int dx, int dy, int w, int h) {
    // Handle overlaps to prevent overwriting source before reading
    int y_start = 0, y_end = h, y_step = 1;
    if (dy > sy) { y_start = h - 1; y_end = -1; y_step = -1; }
    
    int x_start = 0, x_end = w, x_step = 1;
    if (dx > sx) { x_start = w - 1; x_end = -1; x_step = -1; }
    
    for (int i = y_start; i != y_end; i += y_step) {
        for (int j = x_start; j != x_end; j += x_step) {
                uint32_t col = getpixel(sx + j, sy + i);
                putpixel(dx + j, dy + i, col);
        }
    }
}

// Sophisticated pixel getter for the desktop background
// Handles Wallpaper, Taskbar, Start Button, and Icon
uint32_t get_desktop_pixel(int x, int y) {
    // 1. Taskbar Area
    if (y >= (int)fb_height - 40) {
        
        // Start Button: x in [5, 85), y in [fb_height-35, fb_height-5)
        int btn_y = (int)fb_height - 35;
        if (x >= 5 && x < 85 && y >= btn_y && y < btn_y + 30) {
            
            // Highlights/Shadows
            if (y == btn_y || x == 5) return 0xFFFFFF;       // Top/Left Highlight
            if (y == btn_y + 1 || x == 6) return 0xFFFFFF;   // Thicker Highlight
            
            if (y == btn_y + 29 || x == 84) return 0x404040; // Bottom/Right Shadow
            if (y == btn_y + 28 || x == 83) return 0x404040; // Thicker Shadow

            // Icon Drawing
            int icon_w = 16;
            int icon_h = 13;
            int icon_x = 5 + (80 - icon_w) / 2; 
            int icon_y = btn_y + (30 - icon_h) / 2;

            if (x >= icon_x && x < icon_x + icon_w && y >= icon_y && y < icon_y + icon_h) {
                int col = x - icon_x;
                int row = y - icon_y;
                int type = icon_data[row][col];
                
                if (type == 0) return 0x000000;
                if (type == 1) return 0x808080;
                if (type == 2) return 0xFFFFFF;
                // Type 3 is transparent, fall through to button face
            }
            
            return 0xD0D0D0; // Button Face
        }
        
        return 0xC0C0C0; // Taskbar Silver
    }
    
    // 2. Wallpaper
    return 0x008080; // Teal
}

void restore_desktop_rect(int x, int y, int w, int h) {
    // Simple clipping to screen bounds
    int start_x = (x < 0) ? 0 : x;
    int start_y = (y < 0) ? 0 : y;
    int end_x = x + w;
    int end_y = y + h;

    if (end_x > (int)fb_width) end_x = fb_width;
    if (end_y > (int)fb_height) end_y = fb_height;

    for (int cy = start_y; cy < end_y; cy++) {
        for (int cx = start_x; cx < end_x; cx++) {
            // "Paint over" the window with the desktop color
            uint32_t bg_color = get_desktop_pixel(cx, cy);
            putpixel(cx, cy, bg_color);
        }
    }
}

void cleanup_desktop(int old_x, int old_y, int w, int h, int new_x, int new_y) {
    if (w <= 0 || h <= 0) return;

    // Use a slightly larger bounds to ensure shadows are cleared
    // passed w is usually win_w + 5, but let's be safe against corner artifacts
    
    // Top Strip (Moved Down)
    if (new_y > old_y) {
        int strip_h = new_y - old_y;
        if (strip_h > h) strip_h = h;
        restore_desktop_rect(old_x, old_y, w, strip_h);
    }
    // Bottom Strip (Moved Up)
    if (new_y < old_y) {
        int strip_h = old_y - new_y;
        if (strip_h > h) strip_h = h;
        restore_desktop_rect(old_x, new_y + h, w, strip_h);
    }
    // Left Strip (Moved Right)
    if (new_x > old_x) {
        int strip_w = new_x - old_x;
        if (strip_w > w) strip_w = w;
        restore_desktop_rect(old_x, old_y, strip_w, h);
    }
    // Right Strip (Moved Left)
    if (new_x < old_x) {
        int strip_w = old_x - new_x;
        if (strip_w > w) strip_w = w;
        restore_desktop_rect(new_x + w, old_y, strip_w, h);
    }
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            putpixel(x + j, y + i, color);
        }
    }
}

void draw_desktop() {
    // 1. Wallpaper (Teal)
    draw_rect(0, 0, fb_width, fb_height, 0x008080);
    
    // 2. Taskbar (Silver)
    draw_rect(0, fb_height - 40, fb_width, 40, 0xC0C0C0);
    
    // 3. Start Button (Gray button with highlights)
    draw_rect(5, fb_height - 35, 80, 30, 0xD0D0D0);
    draw_rect(5, fb_height - 35, 80, 2, 0xFFFFFF);      // Top Highlight
    draw_rect(5, fb_height - 35, 2, 30, 0xFFFFFF);      // Left Highlight
    draw_rect(5 + 78, fb_height - 35, 2, 30, 0x404040); // Right Shadow
    draw_rect(5, fb_height - 35 + 28, 80, 2, 0x404040); // Bottom Shadow

    // 4. Draw Icon
    // Moved icon_data to global
    int icon_h = 13;
    int icon_w = 16;
    
    int icon_x = 5 + (80 - icon_w) / 2; // Center in button (width 80)
    int icon_y = (fb_height - 35) + (30 - icon_h) / 2; // Center in button (height 30)

    for (int y = 0; y < icon_h; y++) {
        for (int x = 0; x < icon_w; x++) {
            uint32_t color = 0xD0D0D0;
            if (icon_data[y][x] == 0) color = 0x000000;
            else if (icon_data[y][x] == 1) color = 0x808080;
            else if (icon_data[y][x] == 2) color = 0xFFFFFF;
            
            if (icon_data[y][x] != 3) {
                putpixel(icon_x + x, icon_y + y, color);
            }
        }
    }
}

void draw_shell_window() {
    // Window shadow
    draw_rect(win_x + 4, win_y + 4, win_w, win_h, 0x404040);
    // Window Body
    draw_rect(win_x, win_y, win_w, win_h, 0xC0C0C0);
    // Title Bar
    draw_rect(win_x + 2, win_y + 2, win_w - 4, 25, 0x000080); // Classic Blue title
    
    // Text Area (Only draw if NOT dragging - handled elsewhere during drag to preserve text)
    if (!is_dragging) {
        draw_rect(win_x + 5, win_y + 30, win_w - 10, win_h - 35, 0x000000); // Black terminal area
        
        // Initial Refresh to show any prompt
        refresh_terminal();
    }
}

// Special version that just draws the frame borders, used during drag
void draw_window_frame() {
    // Window Body
    draw_rect(win_x, win_y, win_w, win_h, 0xC0C0C0);
    // Title Bar
    draw_rect(win_x + 2, win_y + 2, win_w - 4, 25, 0x000080); 
    // We intentionally SKIP the inner black box so we don't overwrite copied text pixels
}

void handle_mouse() {
    // Poll current state
    int mx, my, btns;
    sys_get_mouse(&mx, &my, &btns);
    
    bool left_down = btns & 1;

    if (left_down && !last_mouse_btn) {
        // Click Start - Check collision with title bar
        if (mx >= win_x + 2 && mx <= win_x + win_w - 2 &&
            my >= win_y + 2 && my <= win_y + 27) {
            is_dragging = true;
            drag_offset_x = mx - win_x;
            drag_offset_y = my - win_y;
        }
    }
    else if (!left_down && last_mouse_btn) {
        // Release
        is_dragging = false;
        
        // Final redraw to ensure crispness
        sys_mouse_hide();
        draw_shell_window();
        refresh_terminal();
        sys_mouse_show();
    }

    if (is_dragging) {
        int new_x = mx - drag_offset_x;
        int new_y = my - drag_offset_y;

        // Constraints
        if (new_x < 0) new_x = 0;
        if (new_y < 0) new_y = 0;
        if (new_x + win_w > 1024) new_x = 1024 - win_w;
        if (new_y + win_h > 768) new_y = 768 - win_h;
        
        if (new_x != win_x || new_y != win_y) {
            
            // 1. HIDE MOUSE (Prevents saving "dirty" pixels)
            sys_mouse_hide();

            // 2. ERASE OLD WINDOW (Restore background)
            restore_desktop_rect(win_x, win_y, win_w + 8, win_h + 8);

            // 3. UPDATE COORDINATES
            win_x = new_x;
            win_y = new_y;

            // 4. DRAW NEW WINDOW
            draw_shell_window();
            refresh_terminal();

            // 5. SHOW MOUSE (Captures new clean state)
            sys_mouse_show();
        }
    }
    
    last_mouse_btn = left_down;
}


void _start() {
    init_graphics();
    draw_shell_window();
    sys_update_window(win_x, win_y, win_w, win_h); // Sync Initial State

    kprint("Welcome to ProjectOS User Terminal!\n");
    kprint("Type 'help' for commands.\n");
    kprint("user % ");

    // Main Loop
    while (1) {
        handle_mouse();

        char c = sys_get_key();
        if (c == 0) continue; // Non-blocking spin wait

        if (c == '\n') {
            kprint_char('\n');
            if (buffer_idx > 0) {
                execute_command(command_buffer);
            } else {
                kprint("user % ");
            }
            
            // Clear buffer
            for (int i = 0; i < MAX_COMMAND_LEN; i++) command_buffer[i] = 0;
            buffer_idx = 0;
        } 
        else if (c == '\b') {
            if (buffer_idx > 0) {
                buffer_idx--;
                command_buffer[buffer_idx] = 0;
                kprint_char('\b');
            }
        }
        else {
            if (buffer_idx < MAX_COMMAND_LEN - 1) {
                command_buffer[buffer_idx++] = c;
                command_buffer[buffer_idx] = '\0';
                kprint_char(c);
            }
        }
    }

    sys_exit();
}
