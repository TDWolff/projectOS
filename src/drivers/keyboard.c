#include "keyboard.h"
#include "vga.h"
#include "shell.h"
#include "terminal_window.h"
#include "../include/ports.h"

// Temporary single-terminal wiring.
// Later: replace this with focused-window input routing.
static terminal_window_t* g_term = 0;

void keyboard_set_terminal_window(void* term) {
    g_term = (terminal_window_t*)term;
}

static int shift_pressed = 0;

// Standard Map
char scancode_to_char[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0
};

// Shifted Map (Corresponds to the same indices)
char scancode_to_char_shift[] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0
};

void keyboard_handler() {
    uint8_t scancode = inb(0x60);

    // 1. Handle Shift Press
    if (scancode == 0x2A || scancode == 0x36) { // Left or Right Shift
        shift_pressed = 1;
        return;
    }

    // 2. Handle Shift Release (0xAA or 0xB6)
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = 0;
        return;
    }

    // 3. Ignore other Key Releases (Bit 7 set)
    if (scancode & 0x80) return;

    // 4. Translate Key
    if (scancode < 59) { // 59 is roughly where function keys start
        char c = 0;
        
        if (shift_pressed) {
            c = scancode_to_char_shift[scancode];
        } else {
            c = scancode_to_char[scancode];
        }

        if (c > 0) {
            shell_update(c);
        }
    }
}

void keyboard_init() {
    while (inb(0x64) & 0x01) inb(0x60); // Flush buffer
    outb(0x60, 0xF4); // Enable scanning
}