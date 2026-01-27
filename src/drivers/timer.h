#ifndef TIMER_H
#define TIMER_H

#include "../include/types.h"

void timer_init(uint32_t freq);
void timer_handler(); // Added this so idt.c can see it
void sleep(uint32_t ticks);
uint64_t get_ticks();

#endif