#include "vga.h"
#include "../include/ports.h"

volatile uint16_t* video_memory = (volatile uint16_t*) 0xb8000;

const int VGA_WIDTH = 80;
const int VGA_HEIGHT = 25;

int terminal_row = 0;
int terminal_col = 0;
uint8_t terminal_color;

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t) uc | (uint16_t) color << 8;
}

void update_cursor(int x, int y) {
    uint16_t pos = y * VGA_WIDTH + x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t) (pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}

void terminal_clear() {
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            video_memory[y * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
        }
    }
    terminal_row = 0;
    terminal_col = 0;
    update_cursor(0, 0);
}

void terminal_initialize() {
    terminal_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    terminal_clear();
}

void terminal_set_color(uint8_t fg, uint8_t bg) {
    terminal_color = fg | bg << 4;
}

void kprint_char(char c) {
    if (c == '\n') {
        terminal_col = 0;
        terminal_row++;
    } else if (c == '\b') {
        if (terminal_col > 0) {
            terminal_col--;
            video_memory[terminal_row * VGA_WIDTH + terminal_col] = vga_entry(' ', terminal_color);
        }
    } else {
        video_memory[terminal_row * VGA_WIDTH + terminal_col] = vga_entry(c, terminal_color);
        terminal_col++;
        if (terminal_col >= VGA_WIDTH) {
            terminal_col = 0;
            terminal_row++;
        }
    }

    if (terminal_row >= VGA_HEIGHT) {
        terminal_row = 0; 
    }
    update_cursor(terminal_col, terminal_row);
}

void kprint(const char* str) {
    for (size_t i = 0; str[i] != '\0'; i++)
        kprint_char(str[i]);
}