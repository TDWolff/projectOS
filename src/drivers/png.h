#ifndef PNG_H
#define PNG_H

// Draw a PNG from the initrd onto the framebuffer.
// mode 0 = native size at (x,y)
// mode 1 = scale-to-fill (aspect fill, centres, may crop edges)
// mode 2 = scale-to-fit  (aspect fit,  centres, may letterbox)
// Returns 1 on success, 0 if the file was not found or could not be decoded.
int png_draw(const char* filename, int x, int y, int mode);

#endif
