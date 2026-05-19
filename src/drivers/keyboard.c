#include "keyboard.h"
#include "shell.h"
#include "window.h"
#include "../include/ports.h"

static int shift_pressed = 0;
static int ctrl_pressed  = 0;
static int e0_pending    = 0;

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

static void send_char(char c) {
    window_t* focused = window_get_focused();
    if (focused && focused->on_char_input) {
        focused->on_char_input(focused, c, focused->on_char_input_user);
    } else {
        shell_update(c);
    }
}

static void send_seq(const char* s) {
    for (; *s; s++) send_char(*s);
}

void keyboard_handler() {
    uint8_t scancode = inb(0x60);

    // E0 prefix: next byte is an extended key
    if (scancode == 0xE0) {
        e0_pending = 1;
        return;
    }

    if (e0_pending) {
        e0_pending = 0;
        if (scancode & 0x80) {
            // Extended key release
            if ((scancode & 0x7F) == 0x1D) ctrl_pressed = 0; // right Ctrl release
            return;
        }
        switch (scancode) {
            case 0x48: send_seq("\x1B[A");  break; // up
            case 0x50: send_seq("\x1B[B");  break; // down
            case 0x4D: send_seq("\x1B[C");  break; // right
            case 0x4B: send_seq("\x1B[D");  break; // left
            case 0x47: send_seq("\x1B[H");  break; // home
            case 0x4F: send_seq("\x1B[F");  break; // end
            case 0x53: send_seq("\x1B[3~"); break; // delete
            case 0x49: send_seq("\x1B[5~"); break; // page up
            case 0x51: send_seq("\x1B[6~"); break; // page down
            case 0x1D: ctrl_pressed = 1;    break; // right Ctrl
        }
        return;
    }

    // Shift press/release
    if (scancode == 0x2A || scancode == 0x36) { shift_pressed = 1; return; }
    if (scancode == 0xAA || scancode == 0xB6) { shift_pressed = 0; return; }

    // Ctrl press/release (left Ctrl)
    if (scancode == 0x1D) { ctrl_pressed = 1; return; }
    if (scancode == 0x9D) { ctrl_pressed = 0; return; }

    // Ignore other key releases (bit 7 set)
    if (scancode & 0x80) return;

    // Translate key
    if (scancode < 59) {
        char c = shift_pressed ? scancode_to_char_shift[scancode]
                               : scancode_to_char[scancode];
        if (c <= 0) return;

        if (ctrl_pressed) {
            // Map Ctrl+letter to control code (1-26).
            // Works for a-z (lowercase) and A-Z (uppercase with shift).
            char upper = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
            if (upper >= '@' && upper <= '_') {
                send_char((char)(upper & 0x1F));
                return;
            }
            // Other Ctrl combos (digits, punctuation): pass through unchanged
        }

        send_char(c);
    }
}

void keyboard_init() {
    while (inb(0x64) & 0x01) inb(0x60); // Flush buffer
    outb(0x60, 0xF4); // Enable scanning
}
