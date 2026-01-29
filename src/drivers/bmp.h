#ifndef BMP_H
#define BMP_H

#include "../include/types.h"

// Scaling Modes
#define BMP_MODE_NORMAL         0
#define BMP_MODE_SCALE_TO_FIT   1 // "Scale to Fit" - Aspect Fill (Zooms in, cuts off edges)
#define BMP_MODE_FIT            2 // "Fit" - Aspect Fit (Whole image visible, black bars)

// Draws a BMP file from the initrd to the screen
// If mode != BMP_MODE_NORMAL, x and y are ignored
void bmp_draw(const char* filename, int x, int y, int mode);

#endif
