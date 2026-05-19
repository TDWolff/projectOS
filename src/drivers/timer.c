#include "timer.h"
#include "../include/ports.h"
#include "../lib/stdio.h"

// Network polling (e1000 RX)
#include "net/e1000.h"

uint64_t system_ticks = 0;

void timer_handler() {
    system_ticks++;
    // Poll NIC at ~62 Hz (every 16ms) to avoid ISR overhead at 1000 Hz.
    if ((system_ticks & 15) == 0) e1000_poll();
}

void timer_init(uint32_t freq) {
    uint32_t divisor = 1193182 / freq;

    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void sleep(uint32_t ticks) {
    uint64_t start_ticks = system_ticks;
    while (system_ticks < start_ticks + ticks) {
        __asm__ volatile("hlt");
    }
}

uint64_t get_ticks() {
    return system_ticks;
}