#include "pfs.h"

#include "../../drivers/block/ata_pio.h"
#include "../../lib/stdio.h"
#include "../../lib/string.h"
#include <stdarg.h>

#include "mbr_gpt.h"
#include "fat32.h"

// For now we just prove block I/O + persistence using one sector.
// Later, this becomes: GPT -> FAT32 mount at /user.

#define PFS_TEST_LBA 2048u

static pfs_state_t g_pfs = {0};
static fat32_fs_t g_user_fs = {0};

static int to_lower_ascii(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static bool equals_ignore_case(const char* a, const char* b) {
    if (!a || !b) return false;
    int i = 0;
    while (a[i] && b[i]) {
        if (to_lower_ascii(a[i]) != to_lower_ascii(b[i])) return false;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static void trim_leading_slashes(const char** p) {
    while (**p == '/') (*p)++;
}

static void pfs_dump_hex16(const uint8_t* p) {
    for (int i = 0; i < 16; i++) {
        kprintf("%x", (unsigned)((p[i] >> 4) & 0xF));
        kprintf("%x", (unsigned)(p[i] & 0xF));
    }
}

static uint16_t rd16le(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd32le(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t rd64le(const uint8_t* p) {
    uint64_t lo = (uint64_t)rd32le(p);
    uint64_t hi = (uint64_t)rd32le(p + 4);
    return lo | (hi << 32);
}

static void pfs_sink_puts(pfs_putc_fn putc_cb, void* user, const char* s) {
    if (!putc_cb || !s) return;
    for (size_t i = 0; s[i]; i++) putc_cb(s[i], user);
}

static void pfs_sink_put_uint(pfs_putc_fn putc_cb, void* user, uint64_t val, int base, bool uppercase) {
    char buf[32];
    int i = 0;
    if (val == 0) {
        putc_cb('0', user);
        return;
    }
    while (val && i < (int)sizeof(buf)) {
        uint64_t d = val % (uint64_t)base;
        char c;
        if (d < 10) c = (char)('0' + d);
        else c = (char)((uppercase ? 'A' : 'a') + (d - 10));
        buf[i++] = c;
        val /= (uint64_t)base;
    }
    while (i--) putc_cb(buf[i], user);
}

static void pfs_sink_put_int(pfs_putc_fn putc_cb, void* user, int64_t v, int base) {
    if (v < 0) {
        putc_cb('-', user);
        pfs_sink_put_uint(putc_cb, user, (uint64_t)(-v), base, false);
    } else {
        pfs_sink_put_uint(putc_cb, user, (uint64_t)v, base, false);
    }
}

static void pfs_sink_vprintf(pfs_putc_fn putc_cb, void* user, const char* fmt, va_list ap) {
    if (!putc_cb || !fmt) return;
    for (size_t i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') {
            putc_cb(fmt[i], user);
            continue;
        }
        i++;
        char f = fmt[i];
        if (!f) break;
        switch (f) {
            case '%': putc_cb('%', user); break;
            case 'c': {
                int c = va_arg(ap, int);
                putc_cb((char)c, user);
            } break;
            case 's': {
                const char* s = va_arg(ap, const char*);
                pfs_sink_puts(putc_cb, user, s ? s : "(null)");
            } break;
            case 'd': {
                int v = va_arg(ap, int);
                pfs_sink_put_int(putc_cb, user, (int64_t)v, 10);
            } break;
            case 'u': {
                unsigned v = va_arg(ap, unsigned);
                pfs_sink_put_uint(putc_cb, user, (uint64_t)v, 10, false);
            } break;
            case 'x': {
                unsigned v = va_arg(ap, unsigned);
                pfs_sink_put_uint(putc_cb, user, (uint64_t)v, 16, false);
            } break;
            case 'X': {
                unsigned v = va_arg(ap, unsigned);
                pfs_sink_put_uint(putc_cb, user, (uint64_t)v, 16, true);
            } break;
            default:
                // Unknown specifier: print literally.
                putc_cb('%', user);
                putc_cb(f, user);
                break;
        }
    }
}

static void pfs_sink_printf(pfs_putc_fn putc_cb, void* user, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    pfs_sink_vprintf(putc_cb, user, fmt, ap);
    va_end(ap);
}

static void pfs_vga_putc_adapter(char c, void* u) {
    (void)u;
    kprint_char(c);
}

static void pfs_print_vbr_bpb(pfs_putc_fn putc_cb, void* user, uint32_t lba) {
    uint8_t vbr[ATA_SECTOR_SIZE];
    if (!ata_pio_read_sector(lba, vbr)) {
        pfs_sink_printf(putc_cb, user, "PFSDBG: VBR read failed at LBA %d\n", (int)lba);
        return;
    }
    pfs_sink_printf(putc_cb, user, "PFSDBG: VBR LBA %d sig=%x%x%x%x\n", (int)lba,
                    (unsigned)((vbr[510] >> 4) & 0xF), (unsigned)(vbr[510] & 0xF),
                    (unsigned)((vbr[511] >> 4) & 0xF), (unsigned)(vbr[511] & 0xF));

    uint16_t bps = rd16le(vbr + 11);
    uint8_t spc = vbr[13];
    uint16_t rsv = rd16le(vbr + 14);
    uint8_t nf = vbr[16];
    uint16_t re = rd16le(vbr + 17);
    uint16_t ts16 = rd16le(vbr + 19);
    uint16_t f16 = rd16le(vbr + 22);
    uint32_t ts32 = rd32le(vbr + 32);
    uint32_t f32 = rd32le(vbr + 36);
    uint32_t rootcl = rd32le(vbr + 44);

    pfs_sink_printf(putc_cb, user,
                    "PFSDBG: BPB bps=%d spc=%d rsv=%d nf=%d rootEnt=%d ts16=%d ts32=%d fat16=%d fat32=%d rootCl=%d\n",
                    (int)bps, (int)spc, (int)rsv, (int)nf, (int)re, (int)ts16, (int)ts32, (int)f16, (int)f32, (int)rootcl);
}

void pfs_debug_probe_and_print_to(pfs_putc_fn putc_cb, void* user) {
    pfs_sink_printf(putc_cb, user, "\n--- PFS DEBUG PROBE START ---\n");

    const ata_pio_device_t* dev = ata_pio_get_device();
    if (!dev || !dev->present) {
        pfs_sink_printf(putc_cb, user, "PFSDBG: ata_pio_get_device says no device present.\n");
        pfs_sink_printf(putc_cb, user, "--- PFS DEBUG PROBE END ---\n");
        return;
    }
    pfs_sink_printf(putc_cb, user, "PFSDBG: ATA present=1 sector_size=%d lba28_max=%d\n", (int)dev->sector_size, (int)dev->lba28_max);

    uint8_t mbr[ATA_SECTOR_SIZE];
    if (!ata_pio_read_sector(0, mbr)) {
        pfs_sink_printf(putc_cb, user, "PFSDBG: read LBA0 failed.\n");
        pfs_sink_printf(putc_cb, user, "--- PFS DEBUG PROBE END ---\n");
        return;
    }
    pfs_sink_printf(putc_cb, user, "PFSDBG: LBA0 sig=%x%x%x%x\n",
                    (unsigned)((mbr[510] >> 4) & 0xF), (unsigned)(mbr[510] & 0xF),
                    (unsigned)((mbr[511] >> 4) & 0xF), (unsigned)(mbr[511] & 0xF));

    bool protective = false;
    for (int i = 0; i < 4; i++) {
        const uint8_t* e = mbr + 0x1BE + i * 16;
        uint8_t ptype = e[4];
        uint32_t pstart = rd32le(e + 8);
        uint32_t psecs = rd32le(e + 12);
    pfs_sink_printf(putc_cb, user, "PFSDBG: MBR part%d type=0x%x start=%d secs=%d\n", i, (unsigned)ptype, (int)pstart, (int)psecs);
        if (ptype == 0xEE) protective = true;
    }

    // GPT header at LBA1
    uint8_t gpth[ATA_SECTOR_SIZE];
    if (ata_pio_read_sector(1, gpth)) {
    pfs_sink_printf(putc_cb, user, "PFSDBG: GPT hdr bytes[0..7]=%c%c%c%c%c%c%c%c\n",
            gpth[0], gpth[1], gpth[2], gpth[3], gpth[4], gpth[5], gpth[6], gpth[7]);

        if (gpth[0] == 'E' && gpth[1] == 'F' && gpth[2] == 'I' && gpth[3] == ' ' &&
            gpth[4] == 'P' && gpth[5] == 'A' && gpth[6] == 'R' && gpth[7] == 'T') {
            uint32_t rev = rd32le(gpth + 8);
            uint32_t hsz = rd32le(gpth + 12);
            uint64_t cur = rd64le(gpth + 24);
            uint64_t bak = rd64le(gpth + 32);
            uint64_t pe_lba = rd64le(gpth + 72);
            uint32_t pe_cnt = rd32le(gpth + 80);
            uint32_t pe_sz = rd32le(gpth + 84);
            pfs_sink_printf(putc_cb, user,
                            "PFSDBG: GPT rev=%d hsz=%d cur=%d bak=%d pe_lba=%d pe_cnt=%d pe_sz=%d protectiveMBR=%d\n",
                            (int)rev, (int)hsz, (int)cur, (int)bak, (int)pe_lba, (int)pe_cnt, (int)pe_sz, (int)(protective ? 1 : 0));

            // Scan first few entries and try VBR/BPB + mount.
            if (pe_lba <= 0x0FFFFFFFULL && pe_sz >= 128 && pe_sz <= 512 && pe_cnt > 0) {
                uint32_t entries_lba = (uint32_t)pe_lba;
                uint32_t entries_per_sector = ATA_SECTOR_SIZE / pe_sz;
                if (entries_per_sector == 0) entries_per_sector = 1;

                uint8_t entsec[ATA_SECTOR_SIZE];
                for (uint32_t idx = 0; idx < pe_cnt && idx < 32; idx++) {
                    uint32_t sec_index = idx / entries_per_sector;
                    uint32_t off = (idx % entries_per_sector) * pe_sz;

                    if ((idx % entries_per_sector) == 0) {
                        if (!ata_pio_read_sector(entries_lba + sec_index, entsec)) {
                            pfs_sink_printf(putc_cb, user, "PFSDBG: failed to read GPT entries sector %d\n", (int)sec_index);
                            break;
                        }
                    }

                    const uint8_t* ent = entsec + off;
                    const uint8_t* type_guid = ent;
                    bool zero = true;
                    for (int b = 0; b < 16; b++) if (type_guid[b] != 0) { zero = false; break; }
                    if (zero) continue;

                    uint64_t first = rd64le(ent + 32);
                    uint64_t last = rd64le(ent + 40);
                    pfs_sink_printf(putc_cb, user, "PFSDBG: GPT entry %d first=%d last=%d typeGuid=", (int)idx, (int)first, (int)last);
                    if (putc_cb) {
                        for (int bi = 0; bi < 16; bi++) {
                            uint8_t b = type_guid[bi];
                            const char* hx = "0123456789abcdef";
                            putc_cb(hx[(b >> 4) & 0xF], user);
                            putc_cb(hx[b & 0xF], user);
                        }
                    }
                    pfs_sink_printf(putc_cb, user, "\n");

                    if (first <= 0x0FFFFFFFULL) {
                        pfs_print_vbr_bpb(putc_cb, user, (uint32_t)first);
                        // Attempt mount right now.
                        fat32_fs_t tmp;
                        if (fat32_mount(&tmp, (uint32_t)first)) {
                            pfs_sink_printf(putc_cb, user, "PFSDBG: fat32_mount SUCCESS at GPT first LBA %d\n", (int)(uint32_t)first);
                        } else {
                            pfs_sink_printf(putc_cb, user, "PFSDBG: fat32_mount FAIL at GPT first LBA %d\n", (int)(uint32_t)first);
                        }
                    }
                }
            }
        } else {
            pfs_sink_printf(putc_cb, user, "PFSDBG: No 'EFI PART' at LBA1\n");
        }
    } else {
        pfs_sink_printf(putc_cb, user, "PFSDBG: read LBA1 failed.\n");
    }

    // Always probe LBA2048 because it's common and matches your disk image.
    pfs_print_vbr_bpb(putc_cb, user, 2048u);
    fat32_fs_t tmp;
    if (fat32_mount(&tmp, 2048u)) {
        pfs_sink_printf(putc_cb, user, "PFSDBG: fat32_mount SUCCESS at LBA2048\n");
    } else {
        pfs_sink_printf(putc_cb, user, "PFSDBG: fat32_mount FAIL at LBA2048\n");
    }

    pfs_sink_printf(putc_cb, user, "--- PFS DEBUG PROBE END ---\n\n");
}

void pfs_debug_probe_and_print() {
    // Fallback behavior: print to VGA via kprintf.
    // Note: This path is not scrollback-friendly.
    // We still keep it for early boot/debug, but shell should use *_to.
    pfs_debug_probe_and_print_to(pfs_vga_putc_adapter, 0);
}

void pfs_init() {
    g_pfs.has_disk = ata_pio_init();
    if (g_pfs.has_disk) {
        kprintf("PFS: ATA disk detected.\n");
    } else {
        kprintf("PFS: No ATA disk detected (persistence disabled).\n");
    }

    g_pfs.user_mounted = false;
    memset(&g_user_fs, 0, sizeof(g_user_fs));

    if (g_pfs.has_disk) {
        kprintf("PFS: probing /user mount...\n");

        // Try GPT->FAT32 first.
        uint64_t start = 0;
        uint64_t last = 0;
        if (gpt_find_fat32_partition(&start, &last)) {
            (void)last;
            kprintf("PFS: GPT candidate partition start LBA=%d\n", (int)(uint32_t)start);
            if (start <= 0x0FFFFFFFULL && fat32_mount(&g_user_fs, (uint32_t)start)) {
                g_pfs.user_mounted = true;
                kprintf("PFS: /user mounted (FAT32).\n");
            } else {
                kprintf("PFS: FAT32 mount failed.\n");
            }
        } else {
            kprintf("PFS: no GPT FAT32 candidate found.\n");
        }

        // Fallback #1: "superfloppy" FAT32 at LBA0.
        if (!g_pfs.user_mounted) {
            kprintf("PFS: trying FAT32 mount at LBA0...\n");
            if (fat32_mount(&g_user_fs, 0)) {
                g_pfs.user_mounted = true;
                kprintf("PFS: /user mounted (FAT32 @ LBA0).\n");
            } else {
                kprintf("PFS: FAT32 mount at LBA0 failed.\n");
            }
        }

        // Fallback #1.5: common GPT-aligned first partition start (2048).
        // Your current disk image uses a GPT partition starting at LBA 2048.
        // Even if GPT scanning or partition typing changes, probing this LBA
        // makes the mount path more resilient.
        if (!g_pfs.user_mounted) {
            kprintf("PFS: trying FAT32 mount at LBA2048...\n");
            if (fat32_mount(&g_user_fs, 2048u)) {
                g_pfs.user_mounted = true;
                kprintf("PFS: /user mounted (FAT32 @ LBA2048).\n");
            } else {
                kprintf("PFS: FAT32 mount at LBA2048 failed.\n");
            }
        }

        // Fallback #2: MBR partition table (type 0x0B/0x0C) -> FAT32.
        if (!g_pfs.user_mounted) {
            kprintf("PFS: trying MBR FAT32 partitions...\n");
            uint8_t mbr[ATA_SECTOR_SIZE];
            if (ata_pio_read_sector(0, mbr)) {
                if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
                    // 4 partition entries at 0x1BE.
                    for (int i = 0; i < 4 && !g_pfs.user_mounted; i++) {
                        uint8_t* e = mbr + 0x1BE + i * 16;
                        uint8_t ptype = e[4];
                        uint32_t pstart = 0;
                        memcpy(&pstart, e + 8, sizeof(uint32_t));

                        kprintf("PFS: MBR part %d type=0x%x start=%d\n", i, (unsigned)ptype, (int)pstart);

                        // FAT32 CHS (0x0B) or FAT32 LBA (0x0C)
                        if ((ptype == 0x0B || ptype == 0x0C) && pstart != 0) {
                            if (fat32_mount(&g_user_fs, pstart)) {
                                g_pfs.user_mounted = true;
                                kprintf("PFS: /user mounted (FAT32 @ MBR LBA %d).\n", (int)pstart);
                            }
                        }
                    }
                    if (!g_pfs.user_mounted) {
                        kprintf("PFS: no mountable FAT32 MBR partitions found.\n");
                    }
                } else {
                    kprintf("PFS: LBA0 doesn't look like MBR (missing 0x55AA).\n");
                }
            } else {
                kprintf("PFS: failed to read LBA0 for MBR probing.\n");
            }
        }

        if (!g_pfs.user_mounted) {
            kprintf("PFS: /user mount failed (no supported FAT32 layout detected).\n");
        }
    }
}

const pfs_state_t* pfs_get_state() {
    return &g_pfs;
}

static uint32_t crc32_simple(const uint8_t* data, uint32_t len) {
    // Not a real CRC32; just a simple rolling hash to detect readback errors.
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

bool pfs_persist_smoketest() {
    if (!g_pfs.has_disk) return false;

    uint8_t sector[ATA_SECTOR_SIZE];
    memset(sector, 0, sizeof(sector));

    const char* magic = "PROJECTOS_PFS_TEST";
    for (int i = 0; magic[i] && i < 64; i++) sector[i] = (uint8_t)magic[i];

    // Put a changing value so repeated boots show it sticks.
    // We don't have an RTC epoch yet; this is just a constant marker + hash.
    uint32_t hash = crc32_simple(sector, 64);
    memcpy(sector + 64, &hash, sizeof(hash));

    if (!ata_pio_write_sector(PFS_TEST_LBA, sector)) {
        kprintf("PFS: smoketest write failed.\n");
        return false;
    }

    uint8_t readback[ATA_SECTOR_SIZE];
    memset(readback, 0, sizeof(readback));

    if (!ata_pio_read_sector(PFS_TEST_LBA, readback)) {
        kprintf("PFS: smoketest read failed.\n");
        return false;
    }

    for (int i = 0; i < ATA_SECTOR_SIZE; i++) {
        if (sector[i] != readback[i]) {
        kprintf("PFS: smoketest mismatch (data did not roundtrip).\n");
        return false;
    }

    }

    kprintf("PFS: smoketest OK (LBA %d).\n", (int)PFS_TEST_LBA);
    return true;
}

bool pfs_read_user_file(const char* path, uint8_t** out_buf, uint32_t* out_size) {
    if (!path || !out_buf || !out_size) return false;
    *out_buf = 0;
    *out_size = 0;

    if (!g_pfs.user_mounted) return false;

    // Accept: /user/foo or user/foo
    const char* p = path;
    trim_leading_slashes(&p);

    // split first component
    char comp[16];
    int ci = 0;
    while (p[0] && p[0] != '/' && ci < (int)(sizeof(comp) - 1)) {
        comp[ci++] = p[0];
        p++;
    }
    comp[ci] = 0;

    if (!equals_ignore_case(comp, "user")) return false;
    trim_leading_slashes(&p);

    // For now we only support root dir, so filename must not contain '/'.
    for (int i = 0; p[i]; i++) {
        if (p[i] == '/') return false;
    }

    // Prefer long filenames (VFAT) so user-visible paths match what was written.
    // Falls back to 8.3 matching if no LFN entries exist.
    return fat32_read_root_file_long(&g_user_fs, p, out_buf, out_size);
}

bool pfs_list_user_root(pfs_list_cb_t cb, void* user) {
    if (!cb) return false;
    if (!g_pfs.user_mounted) return false;
    return fat32_list_root(&g_user_fs, (fat32_list_cb_t)cb, user);
}

bool pfs_list_user_root_long(pfs_list_lfn_cb_t cb, void* user) {
    if (!cb) return false;
    if (!g_pfs.user_mounted) return false;
    return fat32_list_root_long(&g_user_fs, (fat32_list_lfn_cb_t)cb, user);
}

bool pfs_list_user_root_info(pfs_list_info_cb_t cb, void* user) {
    if (!cb) return false;
    if (!g_pfs.user_mounted) return false;
    return fat32_list_root_info(&g_user_fs, (fat32_list_info_cb_t)cb, user);
}

bool pfs_write_user_file(const char* path, const uint8_t* data, uint32_t size) {
    if (!path || !data) return false;
    if (!g_pfs.user_mounted) return false;

    const char* p = path;
    trim_leading_slashes(&p);

    // Strip "user/" prefix
    char comp[16];
    int ci = 0;
    while (p[0] && p[0] != '/' && ci < (int)(sizeof(comp) - 1)) {
        comp[ci++] = p[0];
        p++;
    }
    comp[ci] = 0;
    if (!equals_ignore_case(comp, "user")) return false;
    trim_leading_slashes(&p);

    // Only root-directory files supported
    for (int i = 0; p[i]; i++) {
        if (p[i] == '/') return false;
    }
    if (!p[0]) return false;

    return fat32_write_root_file(&g_user_fs, p, data, size);
}
