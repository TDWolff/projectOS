#ifndef PORTS_H
#define PORTS_H
#include "types.h"

uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t data);

// 16-bit port I/O (needed for ATA DATA register correctness/perf)
uint16_t inw(uint16_t port);
void outw(uint16_t port, uint16_t data);
void io_wait(void); // Added this

#endif