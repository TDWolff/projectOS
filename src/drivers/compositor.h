#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "../include/types.h"

void compositor_init(uint32_t width, uint32_t height, uint32_t pitch);
void compositor_swap_buffers();
void* compositor_get_backbuffer();

#endif
