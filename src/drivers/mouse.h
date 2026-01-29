#ifndef MOUSE_H
#define MOUSE_H

#include "../include/types.h"

void mouse_init();
void mouse_handler();
void mouse_draw_to_buffer(uint32_t* buffer, uint32_t pitch, uint32_t bpp_div_8);

void mouse_set_scale(int scale_x10);

int mouse_get_x();
int mouse_get_y();
uint8_t mouse_get_buttons();

#endif