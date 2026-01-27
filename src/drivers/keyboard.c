#include "vga.h"
#include "../include/ports.h"
#include "../lib/stdio.h"

// Basic US-QWERTY
char scancode_to_char[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

void keyboard_handler() {
    // 1. Read the status register
    uint8_t status = inb(0x64);
    
    // 2. Check if the output buffer is full (bit 0)
    if (status & 0x01) {
        uint8_t scancode = inb(0x60);

        if (scancode < 0x80) {
            char c = scancode_to_char[scancode];
            if (c > 0) {
                kprint_char(c);
            }
        }
    }

    // 3. Send EOI to the PIC so it knows we handled the interrupt
    outb(0x20, 0x20);
}