
#include "minipng.h"

#include "string.h"
#include "stdio.h"
#include "../mem/heap.h"

// Full (practical) PNG decode support for this kernel:
// - PNG signature + chunks (IHDR, IDAT, IEND)
// - Color type 6 (RGBA), bit depth 8
// - Non-interlaced
// - zlib + DEFLATE with stored, fixed Huffman, and dynamic Huffman blocks
// - PNG scanline unfiltering
// Output: pixels as 0xAARRGGBB (kmalloc'd)

static uint32_t rd32be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static bool png_sig_ok(const uint8_t* p, uint64_t size) {
    static const uint8_t sig[8] = {0x89,'P','N','G','\r','\n',0x1A,'\n'};
    if (size < 8) return false;
    for (int i = 0; i < 8; i++) if (p[i] != sig[i]) return false;
    return true;
}

// Adler-32 for zlib wrapper
static uint32_t adler32(const uint8_t* data, uint32_t len) {
    const uint32_t MOD = 65521;
    uint32_t a = 1, b = 0;
    for (uint32_t i = 0; i < len; i++) {
        a = (a + data[i]) % MOD;
        b = (b + a) % MOD;
    }
    return (b << 16) | a;
}

static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p - a; if (pa < 0) pa = -pa;
    int pb = p - b; if (pb < 0) pb = -pb;
    int pc = p - c; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static bool unfilter_rgba8(uint8_t* raw, int w, int h) {
    int stride = w * 4;
    for (int y = 0; y < h; y++) {
        uint8_t* line = raw + y * (stride + 1);
        uint8_t filter = line[0];
        uint8_t* cur = line + 1;
        uint8_t* prev = (y > 0) ? (raw + (y - 1) * (stride + 1) + 1) : 0;

        switch (filter) {
            case 0: // None
                break;
            case 1: // Sub
                for (int x = 0; x < stride; x++) {
                    uint8_t left = (x >= 4) ? cur[x - 4] : 0;
                    cur[x] = (uint8_t)(cur[x] + left);
                }
                break;
            case 2: // Up
                if (prev) {
                    for (int x = 0; x < stride; x++) cur[x] = (uint8_t)(cur[x] + prev[x]);
                }
                break;
            case 3: // Average
                for (int x = 0; x < stride; x++) {
                    uint8_t left = (x >= 4) ? cur[x - 4] : 0;
                    uint8_t up = prev ? prev[x] : 0;
                    cur[x] = (uint8_t)(cur[x] + ((left + up) >> 1));
                }
                break;
            case 4: // Paeth
                for (int x = 0; x < stride; x++) {
                    int a = (x >= 4) ? cur[x - 4] : 0;
                    int b = prev ? prev[x] : 0;
                    int c = (prev && x >= 4) ? prev[x - 4] : 0;
                    cur[x] = (uint8_t)(cur[x] + (uint8_t)paeth(a, b, c));
                }
                break;
            default:
                return false;
        }
    }
    return true;
}

// ---------------------------
// DEFLATE implementation
// ---------------------------

typedef struct {
    const uint8_t* p;
    uint32_t remaining;
    uint32_t bitbuf;
    int bits;
} bitreader_t;

static bool br_need(bitreader_t* br, int n) {
    while (br->bits < n) {
        if (br->remaining == 0) return false;
        br->bitbuf |= (uint32_t)(*br->p++) << br->bits;
        br->remaining--;
        br->bits += 8;
    }
    return true;
}

static bool br_read(bitreader_t* br, int n, uint32_t* out) {
    if (!br_need(br, n)) return false;
    if (n == 32) {
        *out = br->bitbuf;
    } else {
        *out = br->bitbuf & ((1u << n) - 1u);
    }
    br->bitbuf >>= n;
    br->bits -= n;
    return true;
}

static bool br_align_byte(bitreader_t* br) {
    int drop = br->bits & 7;
    if (drop) {
        uint32_t tmp;
        return br_read(br, drop, &tmp);
    }
    return true;
}

typedef struct {
    uint16_t sym;
    uint8_t len;
} huff_entry_t;

typedef struct {
    huff_entry_t table[1u << 15];
    uint8_t max_len;
} huff_t;

static uint32_t revbits(uint32_t v, int n) {
    uint32_t r = 0;
    for (int i = 0; i < n; i++) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

static bool huff_build(huff_t* h, const uint8_t* lengths, int num) {
    uint16_t bl_count[16];
    uint16_t next_code[16];
    memset(bl_count, 0, sizeof(bl_count));
    memset(next_code, 0, sizeof(next_code));

    uint8_t max_len = 0;
    for (int i = 0; i < num; i++) {
        uint8_t l = lengths[i];
        if (l > 15) return false;
        if (l) {
            bl_count[l]++;
            if (l > max_len) max_len = l;
        }
    }
    h->max_len = max_len;

    uint16_t code = 0;
    for (int bits = 1; bits <= 15; bits++) {
        code = (uint16_t)((code + bl_count[bits - 1]) << 1);
        next_code[bits] = code;
    }

    for (uint32_t i = 0; i < (1u << 15); i++) {
        h->table[i].sym = 0xFFFF;
        h->table[i].len = 0;
    }

    for (int n = 0; n < num; n++) {
        uint8_t len = lengths[n];
        if (!len) continue;

        uint16_t c = next_code[len]++;
        uint32_t rc = revbits(c, len);
        uint32_t step = 1u << len;
        for (uint32_t i = rc; i < (1u << 15); i += step) {
            h->table[i].sym = (uint16_t)n;
            h->table[i].len = len;
        }
    }

    return true;
}

static bool huff_decode_sym(bitreader_t* br, const huff_t* h, uint32_t* sym_out) {
    if (!br_need(br, 15)) return false;
    uint32_t idx = br->bitbuf & ((1u << 15) - 1u);
    huff_entry_t e = h->table[idx];
    if (!e.len || e.sym == 0xFFFF) return false;
    uint32_t dummy;
    if (!br_read(br, e.len, &dummy)) return false;
    *sym_out = e.sym;
    return true;
}

static bool out_grow(uint8_t** out, uint32_t* cap, uint32_t cur_len, uint32_t need_len) {
    if (need_len <= *cap) return true;
    uint32_t new_cap = (*cap) ? *cap : 4096;
    while (new_cap < need_len) new_cap *= 2;
    uint8_t* nb = (uint8_t*)kmalloc(new_cap);
    if (!nb) return false;
    if (*out && cur_len) memcpy(nb, *out, cur_len);
    if (*out) kfree(*out);
    *out = nb;
    *cap = new_cap;
    return true;
}

static const uint16_t LEN_BASE[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const uint8_t LEN_EXTRA[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const uint16_t DIST_BASE[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const uint8_t DIST_EXTRA[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

static bool inflate_deflate(const uint8_t* in, uint32_t in_len, uint8_t** out, uint32_t* out_len) {
    bitreader_t br;
    br.p = in;
    br.remaining = in_len;
    br.bitbuf = 0;
    br.bits = 0;

    uint8_t* obuf = 0;
    uint32_t cap = 0;
    uint32_t olen = 0;

    bool final = false;
    while (!final) {
        uint32_t v;
        if (!br_read(&br, 1, &v)) { if (obuf) kfree(obuf); return false; }
        final = (v != 0);
        if (!br_read(&br, 2, &v)) { if (obuf) kfree(obuf); return false; }
        uint32_t btype = v;

        if (btype == 0) {
            if (!br_align_byte(&br)) { if (obuf) kfree(obuf); return false; }
            if (br.remaining < 4) { if (obuf) kfree(obuf); return false; }
            uint16_t len = (uint16_t)br.p[0] | ((uint16_t)br.p[1] << 8);
            uint16_t nlen = (uint16_t)br.p[2] | ((uint16_t)br.p[3] << 8);
            br.p += 4;
            br.remaining -= 4;
            if ((uint16_t)(len ^ 0xFFFF) != nlen) { if (obuf) kfree(obuf); return false; }
            if (br.remaining < len) { if (obuf) kfree(obuf); return false; }
            if (!out_grow(&obuf, &cap, olen, olen + len)) { if (obuf) kfree(obuf); return false; }
            memcpy(obuf + olen, br.p, len);
            olen += len;
            br.p += len;
            br.remaining -= len;
            continue;
        }

        huff_t litlen;
        huff_t dist;
        uint8_t litlen_lengths[288];
        uint8_t dist_lengths[32];

        if (btype == 1) {
            for (int i = 0; i <= 143; i++) litlen_lengths[i] = 8;
            for (int i = 144; i <= 255; i++) litlen_lengths[i] = 9;
            for (int i = 256; i <= 279; i++) litlen_lengths[i] = 7;
            for (int i = 280; i <= 287; i++) litlen_lengths[i] = 8;
            for (int i = 0; i < 32; i++) dist_lengths[i] = 5;
            if (!huff_build(&litlen, litlen_lengths, 288)) { if (obuf) kfree(obuf); return false; }
            if (!huff_build(&dist, dist_lengths, 32)) { if (obuf) kfree(obuf); return false; }
        } else if (btype == 2) {
            uint32_t HLIT, HDIST, HCLEN;
            if (!br_read(&br, 5, &HLIT)) { if (obuf) kfree(obuf); return false; }
            if (!br_read(&br, 5, &HDIST)) { if (obuf) kfree(obuf); return false; }
            if (!br_read(&br, 4, &HCLEN)) { if (obuf) kfree(obuf); return false; }
            HLIT += 257;
            HDIST += 1;
            HCLEN += 4;

            static const uint8_t CL_ORDER[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            uint8_t cl_lengths[19];
            memset(cl_lengths, 0, sizeof(cl_lengths));
            for (uint32_t i = 0; i < HCLEN; i++) {
                uint32_t l;
                if (!br_read(&br, 3, &l)) { if (obuf) kfree(obuf); return false; }
                cl_lengths[CL_ORDER[i]] = (uint8_t)l;
            }

            huff_t cl;
            if (!huff_build(&cl, cl_lengths, 19)) { if (obuf) kfree(obuf); return false; }

            uint32_t total = HLIT + HDIST;
            uint8_t lengths[288 + 32];
            memset(lengths, 0, sizeof(lengths));
            uint32_t idx = 0;
            while (idx < total) {
                uint32_t sym;
                if (!huff_decode_sym(&br, &cl, &sym)) { if (obuf) kfree(obuf); return false; }
                if (sym <= 15) {
                    lengths[idx++] = (uint8_t)sym;
                } else if (sym == 16) {
                    uint32_t rep;
                    if (!br_read(&br, 2, &rep)) { if (obuf) kfree(obuf); return false; }
                    rep += 3;
                    uint8_t prev = (idx > 0) ? lengths[idx - 1] : 0;
                    for (uint32_t i = 0; i < rep && idx < total; i++) lengths[idx++] = prev;
                } else if (sym == 17) {
                    uint32_t rep;
                    if (!br_read(&br, 3, &rep)) { if (obuf) kfree(obuf); return false; }
                    rep += 3;
                    for (uint32_t i = 0; i < rep && idx < total; i++) lengths[idx++] = 0;
                } else if (sym == 18) {
                    uint32_t rep;
                    if (!br_read(&br, 7, &rep)) { if (obuf) kfree(obuf); return false; }
                    rep += 11;
                    for (uint32_t i = 0; i < rep && idx < total; i++) lengths[idx++] = 0;
                } else {
                    if (obuf) kfree(obuf);
                    return false;
                }
            }

            for (uint32_t i = 0; i < HLIT; i++) litlen_lengths[i] = lengths[i];
            for (uint32_t i = 0; i < HDIST; i++) dist_lengths[i] = lengths[HLIT + i];
            for (int i = (int)HLIT; i < 288; i++) litlen_lengths[i] = 0;
            for (int i = (int)HDIST; i < 32; i++) dist_lengths[i] = 0;
            if (!huff_build(&litlen, litlen_lengths, 288)) { if (obuf) kfree(obuf); return false; }
            if (!huff_build(&dist, dist_lengths, 32)) { if (obuf) kfree(obuf); return false; }
        } else {
            if (obuf) kfree(obuf);
            return false;
        }

        while (1) {
            uint32_t sym;
            if (!huff_decode_sym(&br, &litlen, &sym)) { if (obuf) kfree(obuf); return false; }
            if (sym < 256) {
                if (!out_grow(&obuf, &cap, olen, olen + 1)) { if (obuf) kfree(obuf); return false; }
                obuf[olen++] = (uint8_t)sym;
            } else if (sym == 256) {
                break;
            } else if (sym <= 285) {
                uint32_t lcode = sym - 257;
                uint32_t extra = LEN_EXTRA[lcode];
                uint32_t extra_val = 0;
                if (extra) {
                    if (!br_read(&br, (int)extra, &extra_val)) { if (obuf) kfree(obuf); return false; }
                }
                uint32_t length = (uint32_t)LEN_BASE[lcode] + extra_val;
                uint32_t dsym;
                if (!huff_decode_sym(&br, &dist, &dsym)) { if (obuf) kfree(obuf); return false; }
                if (dsym > 29) { if (obuf) kfree(obuf); return false; }
                uint32_t dextra = DIST_EXTRA[dsym];
                uint32_t dextra_val = 0;
                if (dextra) {
                    if (!br_read(&br, (int)dextra, &dextra_val)) { if (obuf) kfree(obuf); return false; }
                }
                uint32_t distance = (uint32_t)DIST_BASE[dsym] + dextra_val;
                if (distance == 0 || distance > olen) { if (obuf) kfree(obuf); return false; }
                if (!out_grow(&obuf, &cap, olen, olen + length)) { if (obuf) kfree(obuf); return false; }
                uint32_t src = olen - distance;
                for (uint32_t i = 0; i < length; i++) {
                    obuf[olen++] = obuf[src++];
                    if (src >= olen) src = olen - distance;
                }
            } else {
                if (obuf) kfree(obuf);
                return false;
            }
        }
    }

    *out = obuf;
    *out_len = olen;
    return true;
}

static bool zlib_inflate_full(const uint8_t* z, uint32_t zlen, uint8_t** out, uint32_t* out_len) {
    if (zlen < 6) return false;
    uint8_t cmf = z[0];
    uint8_t flg = z[1];
    if ((cmf & 0x0F) != 8) return false;
    if (((uint16_t)cmf * 256 + flg) % 31 != 0) return false;
    if (flg & 0x20) return false;

    if (zlen < 2 + 4) return false;
    const uint8_t* def = z + 2;
    uint32_t def_len = zlen - 2 - 4;

    uint8_t* raw = 0;
    uint32_t raw_len = 0;
    if (!inflate_deflate(def, def_len, &raw, &raw_len)) return false;
    uint32_t expected = rd32be(z + zlen - 4);
    uint32_t got = adler32(raw, raw_len);
    if (expected != got) {
        kfree(raw);
        return false;
    }
    *out = raw;
    *out_len = raw_len;
    return true;
}

bool png_decode_rgba32(const void* data, uint64_t size, png_image_t* out) {
    if (!out) return false;
    out->pixels = 0;
    out->width = 0;
    out->height = 0;

    const uint8_t* p = (const uint8_t*)data;
    if (!png_sig_ok(p, size)) return false;

    uint32_t w = 0, h = 0;
    uint8_t bit_depth = 0, color_type = 0, interlace = 0;

    uint32_t idat_cap = 0;
    uint32_t idat_len = 0;
    uint8_t* idat = 0;

    uint64_t off = 8;
    while (off + 8 <= size) {
        uint32_t chunk_len = rd32be(p + off);
        uint32_t chunk_type = rd32be(p + off + 4);
        off += 8;
        if (off + (uint64_t)chunk_len + 4 > size) { if (idat) kfree(idat); return false; }

        const uint8_t* chunk = p + off;

        if (chunk_type == 0x49484452u) { // IHDR
            if (chunk_len < 13) { if (idat) kfree(idat); return false; }
            w = rd32be(chunk + 0);
            h = rd32be(chunk + 4);
            bit_depth = chunk[8];
            color_type = chunk[9];
            interlace = chunk[12];
            if (w == 0 || h == 0) { if (idat) kfree(idat); return false; }
            if (bit_depth != 8 || color_type != 6) { if (idat) kfree(idat); return false; }
            if (interlace != 0) { if (idat) kfree(idat); return false; }
        } else if (chunk_type == 0x49444154u) { // IDAT
            if (chunk_len) {
                if (idat_len + chunk_len > idat_cap) {
                    uint32_t new_cap = idat_cap ? idat_cap : 4096;
                    while (idat_len + chunk_len > new_cap) new_cap *= 2;
                    uint8_t* nb = (uint8_t*)kmalloc(new_cap);
                    if (!nb) { if (idat) kfree(idat); return false; }
                    if (idat && idat_len) memcpy(nb, idat, idat_len);
                    if (idat) kfree(idat);
                    idat = nb;
                    idat_cap = new_cap;
                }
                memcpy(idat + idat_len, chunk, chunk_len);
                idat_len += chunk_len;
            }
        } else if (chunk_type == 0x49454E44u) { // IEND
            break;
        }

        off += (uint64_t)chunk_len + 4; // data + crc
    }

    if (!w || !h || !idat || idat_len < 6) { if (idat) kfree(idat); return false; }

    uint8_t* raw = 0;
    uint32_t raw_len = 0;
    if (!zlib_inflate_full(idat, idat_len, &raw, &raw_len)) {
        kfree(idat);
        return false;
    }
    kfree(idat);

    uint32_t expected_raw = (uint32_t)h * (uint32_t)(1 + (w * 4));
    if (raw_len != expected_raw) {
        kfree(raw);
        return false;
    }
    if (!unfilter_rgba8(raw, (int)w, (int)h)) {
        kfree(raw);
        return false;
    }

    uint32_t* pixels = (uint32_t*)kmalloc((uint64_t)w * (uint64_t)h * 4);
    if (!pixels) { kfree(raw); return false; }
    int stride = (int)w * 4;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* line = raw + y * (stride + 1) + 1;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t r = line[x * 4 + 0];
            uint8_t g = line[x * 4 + 1];
            uint8_t b = line[x * 4 + 2];
            uint8_t a = line[x * 4 + 3];
            pixels[y * w + x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    kfree(raw);
    out->pixels = pixels;
    out->width = (int)w;
    out->height = (int)h;
    return true;
}
