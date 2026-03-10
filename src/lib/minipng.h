#ifndef MINIPNG_H
#define MINIPNG_H

#include "../include/types.h"

// Minimal PNG decoder for 8-bit RGBA PNGs.
// Supports:
// - Color type 6 (RGBA), bit depth 8
// - Non-interlaced
// - DEFLATE streams with *uncompressed* blocks only (BTYPE=0)
// This is enough to support many tiny assets *if* they are saved with "no compression".
// For full PNG support, extend the inflate implementation (Huffman blocks).

typedef struct {
    uint32_t* pixels; // 0xAARRGGBB, kmalloc'd
    int width;
    int height;
} png_image_t;

bool png_decode_rgba32(const void* data, uint64_t size, png_image_t* out);

#endif
