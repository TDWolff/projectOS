#include "keyboard.h"
#include "vga.h"
#include "shell.h"
#include "../include/ports.h"

char scancode_to_char[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

void keyboard_handler() {
    uint8_t scancode = inb(0x60);
    if (!(scancode & 0x80)) { // Key press
        if (scancode < 128) {
            char c = scancode_to_char[scancode];
            if (c > 0) shell_update(c);
        }
    }
}

void keyboard_init() {
    // 1. Drain the controller buffer to clear any leftover bootloader data
    while (inb(0x64) & 0x01) {
        inb(0x60);
    }
    
    // 2. Send the 'Enable Scanning' command (0xF4)
    outb(0x60, 0xF4);
}