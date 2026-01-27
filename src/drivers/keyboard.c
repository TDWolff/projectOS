#include "keyboard.h"
#include "vga.h"
#include "../include/ports.h"

char scancode_to_char[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

void keyboard_handler() {
    uint8_t scancode = inb(0x60);

    // Ignore key releases
    if (scancode & 0x80) {
        return;
    }

    if (scancode < 128) {
        char c = scancode_to_char[scancode];
        if (c > 0) {
            kprint_char(c);
        }
    }
}