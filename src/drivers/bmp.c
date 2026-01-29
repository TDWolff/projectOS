#include "bmp.h"
#include "../fs/initrd.h"
#include "vga.h" // Changed from video.h to vga.h since that's where the definition usually is
#include "../lib/stdio.h"

#pragma pack(push, 1)
typedef struct {
    uint16_t type;              // Magic identifier: 0x4d42 ('BM')
    uint32_t size;              // File size in bytes
    uint16_t reserved1;         // Not used
    uint16_t reserved2;         // Not used
    uint32_t offset;            // Offset to image data in bytes
} bmp_file_header_t;

typedef struct {
    uint32_t size;              // Header size in bytes
    int32_t  width;             // Width of the image
    int32_t  height;            // Height of the image
    uint16_t planes;            // Number of color planes
    uint16_t bits;              // Bits per pixel
    uint32_t compression;       // Compression type
    uint32_t imagesize;         // Image size in bytes
    int32_t  xresolution;       // Pixels per meter
    int32_t  yresolution;       // Pixels per meter
    uint32_t ncolours;          // Number of colors
    uint32_t importantcolours;  // Important colors
} bmp_info_header_t;
#pragma pack(pop)

void bmp_draw(const char* filename, int x, int y, int mode) {
    // 1. Find file in memory
    file_t* file = initrd_open(filename);
    if (!file) {
        kprintf("BMP Error: File '%s' not found.\n", filename);
        return;
    }

    uint8_t* data = (uint8_t*)file->address;
    bmp_file_header_t* file_header = (bmp_file_header_t*)data;
    
    // 2. Validate Helper
    if (file_header->type != 0x4D42) { // 0x4D42 is 'BM' in little endian
        kprintf("BMP Error: Not a valid BMP signature.\n");
        return;
    }

    bmp_info_header_t* info_header = (bmp_info_header_t*)(data + sizeof(bmp_file_header_t));
    
    // We basically only support 24-bit RGB and 32-bit RGBA, no compression (0) or BITFIELDS (3)
    if (info_header->compression != 0 && info_header->compression != 3) {
        kprintf("BMP Error: Compressed BMPs not supported yet.\n");
        return;
    }
    
    // 3. Setup drawing parameters
    uint8_t* pixels = data + file_header->offset;
    int32_t src_width = info_header->width;
    int32_t src_height = info_header->height;
    int bpp = info_header->bits / 8;
    
    if (bpp != 3 && bpp != 4) {
        kprintf("BMP Error: Only 24-bit or 32-bit depths supported.\n");
        return;
    }

    // Determine Destination Scaling
    int32_t dest_x, dest_y, dest_w, dest_h;

    // Default to Source dimensions
    dest_x = x;
    dest_y = y;
    dest_w = src_width;
    dest_h = src_height;

    if (mode != BMP_MODE_NORMAL) {
        int32_t screen_w = (int32_t)get_fb_width();
        int32_t screen_h = (int32_t)get_fb_height();

        // Avoid division by zero
        if (src_width == 0 || src_height == 0) return;

        // Calculate Ratios
        // using 64-bit to prevent overflow during intermediate mult: ratio * 1000
        int64_t ratio_w = ((int64_t)screen_w * 1000) / src_width;
        int64_t ratio_h = ((int64_t)screen_h * 1000) / src_height;
        int64_t ratio;

        if (mode == BMP_MODE_SCALE_TO_FIT) {
             // "Scale to Fit" (Aspect Fill) - Use the LARGER ratio so checking fills one dimension and overflows the other
             ratio = (ratio_w > ratio_h) ? ratio_w : ratio_h;
        } else {
             // "Fit" (Aspect Fit) - Use the SMALLER ratio so image fits entirely inside
             ratio = (ratio_w < ratio_h) ? ratio_w : ratio_h;
        }

        dest_w = (int32_t)((src_width * ratio) / 1000);
        dest_h = (int32_t)((src_height * ratio) / 1000);

        // Center on screen
        dest_x = (screen_w - dest_w) / 2;
        dest_y = (screen_h - dest_h) / 2;
    }

    // BMP rows are padded to 4-byte boundaries
    int row_padded = (src_width * bpp + 3) & (~3);
    
    bool top_down = false;
    if (src_height < 0) {
        src_height = -src_height;
        top_down = true;
    }

    // 4. Draw Scaled Pixels (Start looping over Destination)
    for (int dy = 0; dy < dest_h; dy++) {
        int screen_y = dest_y + dy;
        if (screen_y < 0 || screen_y >= (int)get_fb_height()) continue;

        // Calculate Source Y (Nearest Neighbor)
        // map dy [0..dest_h] -> sy [0..src_height]
        int sy = (int)((int64_t)dy * src_height / dest_h);
        if (sy >= src_height) sy = src_height - 1;

        // Calculate Source Index Y part
        int src_row_idx;
        if (top_down) {
             src_row_idx = sy * row_padded;
        } else {
             // Standard BMP is stored "Upside Down"
             src_row_idx = (src_height - 1 - sy) * row_padded;
        }

        for (int dx = 0; dx < dest_w; dx++) {
             int screen_x = dest_x + dx;
             if (screen_x < 0 || screen_x >= (int)get_fb_width()) continue;

             // Calculate Source X (Nearest Neighbor)
             int sx = (int)((int64_t)dx * src_width / dest_w);
             if (sx >= src_width) sx = src_width - 1;

             // Get Source Pixel
             int src_idx = src_row_idx + (sx * bpp);
             
             // Check bounds
             if (src_idx + bpp > (int)file->size) break;

             uint8_t b = pixels[src_idx];
             uint8_t g = pixels[src_idx + 1];
             uint8_t r = pixels[src_idx + 2];
             uint8_t a = (bpp == 4) ? pixels[src_idx + 3] : 255;
            
             // Draw with alpha blending support
             // Format 0xAARRGGBB
             uint32_t color = (a << 24) | (r << 16) | (g << 8) | b;
             
             if (bpp == 4) {
                  putpixel_alpha(screen_x, screen_y, color);
             } else {
                  putpixel(screen_x, screen_y, color);
             }
        }
    }
}
