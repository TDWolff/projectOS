#ifndef MOUSE_H
#define MOUSE_H

#include "../include/types.h"

void mouse_init();
void mouse_handler();
void draw_mouse_cursor(int x, int y);
void mouse_set_scale(int scale_x10);

int mouse_get_x();
int mouse_get_y();
uint8_t mouse_get_buttons();

#endif