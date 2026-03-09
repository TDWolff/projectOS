#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "../include/types.h"

void keyboard_init();
void keyboard_handler();

// Temporary: route decoded characters to a single terminal window.
// (Eventually this should be focus-based input routing in the window manager.)
void keyboard_set_terminal_window(void* term);

#endif