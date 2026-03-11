#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "../include/types.h"

void keyboard_init();
void keyboard_handler();

// Input routing is focus-based via the window manager.
// `keyboard_handler` will deliver decoded characters to the focused window.

#endif