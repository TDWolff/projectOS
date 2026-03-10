#ifndef ICO_H
#define ICO_H

#include "../include/types.h"

// Minimal ICO decoder (DIB/BMP entries only; no PNG-compressed entries yet).
// Decodes the best-fit icon at or under `max_size`.
//
// Output pixels are 0xAARRGGBB.

typedef struct {
    uint32_t* pixels;
    int width;
    int height;
} ico_image_t;

// Decode an ICO file from memory.
// Returns true on success; caller owns out->pixels (kmalloc) and must kfree.
bool ico_decode_best_fit(const void* data, uint64_t size, int max_size, ico_image_t* out);

#endif
