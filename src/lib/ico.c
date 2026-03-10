#include "ico.h"

#include "string.h"
#include "minipng.h"
#include "../mem/heap.h"

#pragma pack(push, 1)
typedef struct {
    uint16_t reserved; // 0
    uint16_t type;     // 1 = icon
    uint16_t count;
} ico_dir_t;

typedef struct {
    uint8_t width;   // 0 means 256
    uint8_t height;  // 0 means 256
    uint8_t colorCount;
    uint8_t reserved;
    uint16_t planes;
    uint16_t bitCount;
    uint32_t bytesInRes;
    uint32_t imageOffset;
} ico_dir_entry_t;

typedef struct {
    uint32_t size; // header size (40)
    int32_t width;
    int32_t height; // in ICO this is (xor+and) height
    uint16_t planes;
    uint16_t bitCount;
    uint32_t compression;
    uint32_t sizeImage;
    int32_t xPelsPerMeter;
    int32_t yPelsPerMeter;
    uint32_t clrUsed;
    uint32_t clrImportant;
} bmp_info_header_t;
#pragma pack(pop)

static bool decode_dib_32(const uint8_t* dib, uint32_t dib_size, ico_image_t* out) {
    if (dib_size < sizeof(bmp_info_header_t)) return false;

    const bmp_info_header_t* hdr = (const bmp_info_header_t*)dib;
    if (hdr->size < 40) return false;

    int w = hdr->width;
    int h_total = hdr->height;
    if (w <= 0 || h_total == 0) return false;

    // ICO stores height as XOR+AND. Real icon height is half.
    int h = h_total / 2;
    if (h <= 0) return false;

    if (hdr->bitCount != 32) {
        // For now, only 32bpp.
        return false;
    }

    // DIB pixel data begins right after the info header.
    // For 32bpp, it's BGRA little-endian per pixel.
    const uint8_t* px = dib + hdr->size;
    uint32_t px_bytes_needed = (uint32_t)(w * h) * 4;
    if ((uint64_t)hdr->size + px_bytes_needed > dib_size) return false;

    uint32_t* pixels = (uint32_t*)kmalloc((uint64_t)w * (uint64_t)h * 4);
    if (!pixels) return false;

    // Pixels in DIB are stored bottom-up.
    for (int y = 0; y < h; y++) {
        int src_y = (h - 1) - y;
        const uint8_t* row = px + (uint32_t)(src_y * w) * 4;
        for (int x = 0; x < w; x++) {
            uint8_t b = row[x * 4 + 0];
            uint8_t g = row[x * 4 + 1];
            uint8_t r = row[x * 4 + 2];
            uint8_t a = row[x * 4 + 3];
            pixels[y * w + x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    out->pixels = pixels;
    out->width = w;
    out->height = h;
    return true;
}

bool ico_decode_best_fit(const void* data, uint64_t size, int max_size, ico_image_t* out) {
    if (!out) return false;
    out->pixels = 0;
    out->width = 0;
    out->height = 0;

    if (!data || size < sizeof(ico_dir_t)) return false;

    const uint8_t* base = (const uint8_t*)data;
    const ico_dir_t* dir = (const ico_dir_t*)base;

    if (dir->reserved != 0) return false;
    if (dir->type != 1) return false;
    if (dir->count == 0) return false;

    uint64_t entries_off = sizeof(ico_dir_t);
    uint64_t entries_sz = (uint64_t)dir->count * sizeof(ico_dir_entry_t);
    if (entries_off + entries_sz > size) return false;

    // Choose the largest icon <= max_size, else choose the smallest available.
    int best_idx = -1;
    int best_area = -1;
    int fallback_idx = -1;
    int fallback_area = 0x7FFFFFFF;

    for (int i = 0; i < (int)dir->count; i++) {
        const ico_dir_entry_t* e = (const ico_dir_entry_t*)(base + entries_off + (uint64_t)i * sizeof(ico_dir_entry_t));
        int w = (e->width == 0) ? 256 : e->width;
        int h = (e->height == 0) ? 256 : e->height;
        int area = w * h;

        if (w <= max_size && h <= max_size) {
            if (area > best_area) {
                best_area = area;
                best_idx = i;
            }
        }

        if (area < fallback_area) {
            fallback_area = area;
            fallback_idx = i;
        }
    }

    int idx = (best_idx >= 0) ? best_idx : fallback_idx;
    if (idx < 0) return false;

    const ico_dir_entry_t* e = (const ico_dir_entry_t*)(base + entries_off + (uint64_t)idx * sizeof(ico_dir_entry_t));
    uint64_t img_off = e->imageOffset;
    uint64_t img_sz = e->bytesInRes;

    if (img_off + img_sz > size) return false;

    // DIB/BMP entry begins with BITMAPINFOHEADER. PNG entries begin with PNG signature.
    const uint8_t* img = base + img_off;
    if (img_sz >= 8 && img[0] == 0x89 && img[1] == 'P' && img[2] == 'N' && img[3] == 'G') {
        png_image_t png;
        if (!png_decode_rgba32(img, img_sz, &png)) {
            return false;
        }

        out->pixels = png.pixels;
        out->width = png.width;
        out->height = png.height;
        return true;
    }

    return decode_dib_32(img, (uint32_t)img_sz, out);
}
