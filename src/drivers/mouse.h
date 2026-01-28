#ifndef MOUSE_H
#define MOUSE_H

#include "../include/types.h"

void mouse_init();
void mouse_handler();

void mouse_hide();
void mouse_show();

int mouse_get_x();
int mouse_get_y();
uint8_t mouse_get_buttons();

#endif