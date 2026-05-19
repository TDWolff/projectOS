// PNG wallpaper loader — compiled with -isystem src/net/mbedtls_compat so that
// stb_image's standard-library includes resolve to our bare-metal stubs.

#include "../mem/heap.h"    // heap_node_t, kmalloc, kfree
#include "../lib/string.h"  // memcpy

// Proper realloc: reads old size from the heap block header so we never
// copy past the original allocation.  Must be defined before stb_image.h
// expands STBI_REALLOC inside the implementation.
static void* stbi_realloc_impl(void* ptr, size_t new_size) {
    if (!ptr)      return kmalloc((uint64_t)new_size);
    if (!new_size) { kfree(ptr); return (void*)0; }
    heap_node_t* hdr  = (heap_node_t*)((uint8_t*)ptr - sizeof(heap_node_t));
    uint64_t     old  = hdr->size;
    void*        np   = kmalloc((uint64_t)new_size);
    if (!np) return (void*)0;
    memcpy(np, ptr, (size_t)(old < (uint64_t)new_size ? old : (uint64_t)new_size));
    kfree(ptr);
    return np;
}

// --- stb_image configuration ---
#define STBI_NO_STDIO
#define STBI_ONLY_PNG           // compile PNG decoder only — keeps binary small
#define STBI_NO_HDR             // no float HDR paths → no math.h dependency
#define STBI_NO_LINEAR          // no linear-light conversion → no pow()
#define STBI_NO_SIMD            // no SSE/NEON — plain C is safer in early boot
#define STBI_NO_THREAD_LOCALS   // no __thread — bare metal is single-threaded
#define STBI_ASSERT(x)          ((void)(x))
#define STBI_MALLOC(sz)         kmalloc((uint64_t)(sz))
#define STBI_FREE(p)            kfree(p)
#define STBI_REALLOC(p, newsz)  stbi_realloc_impl((p), (size_t)(newsz))
#define STB_IMAGE_IMPLEMENTATION
#include "../lib/stb_image.h"

#include "png.h"
#include "vga.h"
#include "../fs/initrd.h"

int png_draw(const char* filename, int x, int y, int mode) {
    file_t* file = initrd_open(filename);
    if (!file || !file->address || file->size == 0) return 0;

    int src_w = 0, src_h = 0, channels = 0;
    unsigned char* img = stbi_load_from_memory(
        (const stbi_uc*)file->address,
        (int)file->size,
        &src_w, &src_h, &channels,
        4   // always decode to RGBA
    );
    if (!img || src_w <= 0 || src_h <= 0) return 0;

    int32_t dest_x = x, dest_y = y;
    int32_t dest_w = src_w, dest_h = src_h;

    if (mode != 0) {
        int32_t sw = (int32_t)get_fb_width();
        int32_t sh = (int32_t)get_fb_height();
        int64_t rw = ((int64_t)sw * 1000) / src_w;
        int64_t rh = ((int64_t)sh * 1000) / src_h;
        // mode 1 = fill (larger ratio, may crop); mode 2 = fit (smaller ratio, may pad)
        int64_t ratio = (mode == 1) ? (rw > rh ? rw : rh) : (rw < rh ? rw : rh);
        dest_w = (int32_t)((src_w * ratio) / 1000);
        dest_h = (int32_t)((src_h * ratio) / 1000);
        dest_x = (sw - dest_w) / 2;
        dest_y = (sh - dest_h) / 2;
    }

    uint32_t fb_w = get_fb_width();
    uint32_t fb_h = get_fb_height();

    for (int dy = 0; dy < dest_h; dy++) {
        int sy = dest_h > 1 ? (int)((int64_t)dy * src_h / dest_h) : 0;
        if (sy >= src_h) sy = src_h - 1;

        int screen_y = dest_y + dy;
        if (screen_y < 0 || screen_y >= (int)fb_h) continue;

        for (int dx = 0; dx < dest_w; dx++) {
            int screen_x = dest_x + dx;
            if (screen_x < 0 || screen_x >= (int)fb_w) continue;

            int sx = dest_w > 1 ? (int)((int64_t)dx * src_w / dest_w) : 0;
            if (sx >= src_w) sx = src_w - 1;

            int      idx = (sy * src_w + sx) * 4;
            uint8_t  r   = img[idx + 0];
            uint8_t  g   = img[idx + 1];
            uint8_t  b   = img[idx + 2];
            uint8_t  a   = img[idx + 3];
            uint32_t col = ((uint32_t)a << 24) | ((uint32_t)r << 16)
                         | ((uint32_t)g << 8)  |  (uint32_t)b;

            if (a == 255)
                putpixel(screen_x, screen_y, col & 0x00FFFFFF);
            else if (a > 0)
                putpixel_alpha(screen_x, screen_y, col);
        }
    }

    stbi_image_free(img);
    return 1;
}
