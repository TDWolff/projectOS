#include "keyboard.h"
#include "vga.h"
#include "shell.h"
#include "../include/ports.h"

static int shift_pressed = 0;

// Circular Buffer for User Input
#define KBD_BUFFER_SIZE 128
static char kbd_buffer[KBD_BUFFER_SIZE];
static int kbd_write_ptr = 0;
static int kbd_read_ptr = 0;

char keyboard_get_key() {
    if (kbd_read_ptr == kbd_write_ptr) return 0;
    
    char c = kbd_buffer[kbd_read_ptr];
    kbd_read_ptr = (kbd_read_ptr + 1) % KBD_BUFFER_SIZE;
    return c;
}

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

// Helper to buffer keys for user apps
#define KEY_BUFFER_SIZE 256
static char key_buffer[KEY_BUFFER_SIZE];
static int key_read_ptr = 0;
static int key_write_ptr = 0;

void shell_buffer_key(char c) {
    int next_write = (key_write_ptr + 1) % KEY_BUFFER_SIZE;
    // Don't overwrite unread keys
    if (next_write != key_read_ptr) {
        key_buffer[key_write_ptr] = c;
        key_write_ptr = next_write;
    }
}

char shell_get_key() {
    if (key_read_ptr == key_write_ptr) return 0; // Buffer empty
    
    char c = key_buffer[key_read_ptr];
    key_read_ptr = (key_read_ptr + 1) % KEY_BUFFER_SIZE;
    return c;
}

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
            // ORIGINAL: shell_update(c);
            
            // NEW: Buffer the key
            int next = (kbd_write_ptr + 1) % KBD_BUFFER_SIZE;
            if (next != kbd_read_ptr) {
                kbd_buffer[kbd_write_ptr] = c;
                kbd_write_ptr = next;
            }
            
            // Also notify kernel shell if desired, but now we focused on apps
            // keeping it for now in case no app is running
            shell_update(c);
        }
    }
}

void keyboard_init() {
    while (inb(0x64) & 0x01) inb(0x60); // Flush buffer
    outb(0x60, 0xF4); // Enable scanning
}