#ifndef PSF_H
#define PSF_H

#include "../include/types.h"

// PSF1 (Legacy)
#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04

typedef struct {
    uint8_t magic[2];
    uint8_t mode;
    uint8_t charsize;
} __attribute__((packed)) psf1_header_t;

// PSF2 (Modern)
#define PSF2_MAGIC0 0x72
#define PSF2_MAGIC1 0xb5
#define PSF2_MAGIC2 0x4a
#define PSF2_MAGIC3 0x86

typedef struct {
    uint32_t magic;         // 0x864ab572 (Little Endian)
    uint32_t version;
    uint32_t headersize;    // Offset to glyphs
    uint32_t flags;
    uint32_t length;        // Number of glyphs
    uint32_t charsize;      // Bytes per glyph
    uint32_t height;        // Height in pixels
    uint32_t width;         // Width in pixels
} __attribute__((packed)) psf2_header_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_glyph;
    void* glyph_buffer;
} font_t;

#endif