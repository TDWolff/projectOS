#include "fat32.h"

#include "../../drivers/block/ata_pio.h"
#include "../../lib/stdio.h"
#include "../../lib/string.h"
#include "../../mem/heap.h"
#include "../../drivers/shell.h"

// Enable verbose debugging for FAT32 directory enumeration.
// When enabled, fat32_list_root_long() prints whether each entry is emitted via
// VFAT LFN (case-preserving) or SFN (8.3, usually uppercase).
// Keep this off by default to avoid spam.
#ifndef FAT32_DEBUG_LS
#define FAT32_DEBUG_LS 0
#endif

#if FAT32_DEBUG_LS
static void fat32_dbg_int10(int v, char out[32]) {
    // itoa prints lower-case digits for bases > 10; base10 is fine here.
    itoa((int64_t)v, out, 10);
}

static void fat32_dbg_ls_lfn(const char* name) {
    shell_out_str("FAT32_ls: LFN  '");
    shell_out_str(name ? name : "");
    shell_out_str("'\n");
}

static void fat32_dbg_ls_sfn(const char* name, int had_lfn, int lfn_len) {
    char b1[32];
    char b2[32];
    fat32_dbg_int10(had_lfn, b1);
    fat32_dbg_int10(lfn_len, b2);

    shell_out_str("FAT32_ls: SFN  '");
    shell_out_str(name ? name : "");
    shell_out_str("' (had_lfn=");
    shell_out_str(b1);
    shell_out_str(" lfn_len=");
    shell_out_str(b2);
    shell_out_str(")\n");
}
#endif

static uint16_t rd16le(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#pragma pack(push, 1)
typedef struct {
    uint8_t jmp_boot[3];
    uint8_t oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    // FAT32 extended
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t bk_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type[8];
    // boot code follows
} fat32_bpb_t;

typedef struct {
    uint8_t name[11];
    uint8_t attr;
    uint8_t ntres;
    uint8_t crt_time_tenths;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;
    uint32_t file_size;
} fat_dirent_t;

// VFAT Long File Name entry (attribute 0x0F)
typedef struct {
    uint8_t ord;
    uint16_t name1[5];
    uint8_t attr;
    uint8_t type;
    uint8_t chksum;
    uint16_t name2[6];
    uint16_t fst_clus_lo;
    uint16_t name3[2];
} fat_lfn_t;
#pragma pack(pop)

static uint32_t cluster_to_lba(const fat32_fs_t* fs, uint32_t cluster) {
    // Cluster numbers start at 2.
    return fs->data_lba + (cluster - 2) * fs->sectors_per_cluster;
}

static uint32_t fs_bytes_per_sector(const fat32_fs_t* fs) {
    return (fs && fs->bytes_per_sector) ? (uint32_t)fs->bytes_per_sector : ATA_SECTOR_SIZE;
}

static bool ata_read512(uint32_t lba, void* out) {
    return ata_pio_read_sector(lba, out);
}

static bool ata_write512(uint32_t lba, const void* data) {
    return ata_pio_write_sector(lba, data);
}

static bool fat32_read_fat_entry(const fat32_fs_t* fs, uint32_t cluster, uint32_t* out_next) {
    // FAT32 entry is 32-bit, low 28 bits are used.
    uint32_t fat_offset = cluster * 4;
    // NOTE: Our block layer only supports 512-byte ATA sectors.
    // Even if the BPB ever reported a different bytes/sector, we must not
    // use it to compute ATA sector indices.
    uint32_t fat_sector = fs->fat_lba + (fat_offset / ATA_SECTOR_SIZE);
    uint32_t ent_off = fat_offset % ATA_SECTOR_SIZE;

    uint8_t sec[ATA_SECTOR_SIZE];
    if (!ata_read512(fat_sector, sec)) return false;

    uint32_t v = 0;
    memcpy(&v, sec + ent_off, sizeof(uint32_t));
    v &= 0x0FFFFFFFUL;
    *out_next = v;
    return true;
}

static bool name_to_83(const char* in, char out11[11]) {
    for (int i = 0; i < 11; i++) out11[i] = ' ';

    int i = 0, j = 0;
    while (in[i] && in[i] != '.' && j < 8) {
        char c = in[i++];
        if (c == '/') return false;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        out11[j++] = c;
    }
    // Skip any remaining base chars to find the dot (handles long basenames)
    while (in[i] && in[i] != '.') i++;

    if (in[i] == '.') {
        i++;
        j = 8;
        int k = 0;
        while (in[i] && k < 3) {
            char c = in[i++];
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            out11[j++] = c;
            k++;
        }
    }
    return true;
}

static void name_83_to_string(const uint8_t in11[11], char out[13]) {
    // Base name (trim trailing spaces)
    int nlen = 8;
    while (nlen > 0 && in11[nlen - 1] == ' ') nlen--;
    int elen = 3;
    while (elen > 0 && in11[8 + elen - 1] == ' ') elen--;

    int o = 0;
    for (int i = 0; i < nlen; i++) out[o++] = (char)in11[i];
    if (elen > 0) {
        out[o++] = '.';
        for (int i = 0; i < elen; i++) out[o++] = (char)in11[8 + i];
    }
    out[o] = 0;
}

static void lfn_reset(char* lfn, int* lfn_len, bool* have_lfn, uint8_t* lfn_chksum) {
    if (lfn && FAT32_LFN_MAX_CHARS > 0) {
        // Initialize buffer to NUL so partial writes always end up as a valid C string.
    // NOTE: lfn is sized as FAT32_LFN_MAX_CHARS, where the last byte is reserved
    // for the NUL terminator.
    memset(lfn, 0, FAT32_LFN_MAX_CHARS);
    }
    if (lfn_len) *lfn_len = 0;
    if (have_lfn) *have_lfn = false;
    if (lfn_chksum) *lfn_chksum = 0;
}

static void lfn_write_code_unit_at(uint16_t cu, char* out, int pos) {
    if (!out || pos < 0 || pos >= (FAT32_LFN_MAX_CHARS - 1)) return;

    // VFAT stores UCS-2/UTF-16LE code units.
    // We only keep ASCII subset for now, but we must preserve the exact case
    // that the on-disk name uses (never force upper/lower here).
    if (cu == 0x0000 || cu == 0xFFFF) {
        // 0x0000 terminator or 0xFFFF padding
        return;
    }
    char c = (cu <= 0x007F) ? (char)cu : '?';

    // Defensive: if something upstream accidentally fed us flipped-case ASCII,
    // don't try to be clever; always emit exactly what we decoded.
    out[pos] = c;
}

static void lfn_finalize(char* lfn, int* lfn_len) {
    if (!lfn || !lfn_len) return;

    // Compute actual length up to first NUL.
    int i = 0;
    while (i < (FAT32_LFN_MAX_CHARS - 1) && lfn[i] != 0) i++;
    *lfn_len = i;
    lfn[i] = 0;
}

static void lfn_consume_entry(const fat_lfn_t* le, char* lfn, int* lfn_len, bool* have_lfn, uint8_t* lfn_chksum) {
    if (!le || !lfn || !lfn_len || !have_lfn) return;

    uint8_t ord = (uint8_t)(le->ord & 0x1F); // strip LAST flag
    bool last = (le->ord & 0x40) != 0;
    if (ord == 0) return;

    // Start of an LFN sequence has 0x40 bit set.
    if (last) {
        lfn_reset(lfn, lfn_len, have_lfn, lfn_chksum);
        *have_lfn = true;
        if (lfn_chksum) *lfn_chksum = le->chksum;
    }

    if (!*have_lfn) return;
    if (lfn_chksum && *lfn_chksum != le->chksum) {
        // checksum mismatch -> discard
        lfn_reset(lfn, lfn_len, have_lfn, lfn_chksum);
        return;
    }

    // Each LFN entry encodes up to 13 characters.
    // On disk, entries appear in *reverse* order: highest ordinal first, then ... down to 1.
    // We want `lfn[0]` to start with ordinal 1, so map ord -> offset using total count.
    // The total number of entries is stored in the LAST entry's ordinal (with 0x40 set).
    static int s_lfn_total_entries = 0;
    if (last) s_lfn_total_entries = (int)ord;
    if (s_lfn_total_entries <= 0) s_lfn_total_entries = (int)ord;
    int base = (s_lfn_total_entries - (int)ord) * 13;
    int p = base;
    for (int i = 0; i < 5; i++) lfn_write_code_unit_at(le->name1[i], lfn, p++);
    for (int i = 0; i < 6; i++) lfn_write_code_unit_at(le->name2[i], lfn, p++);
    for (int i = 0; i < 2; i++) lfn_write_code_unit_at(le->name3[i], lfn, p++);
}

static bool fat32_read_file_by_dirent(const fat32_fs_t* fs, const fat_dirent_t* de, uint8_t** out_buf, uint32_t* out_size) {
    if (!fs || !de || !out_buf || !out_size) return false;
    *out_buf = 0;
    *out_size = 0;

    uint32_t first_cluster = ((uint32_t)de->fst_clus_hi << 16) | (uint32_t)de->fst_clus_lo;
    uint32_t size = de->file_size;
    if (size == 0) {
        *out_buf = 0;
        *out_size = 0;
        return true;
    }

    uint8_t* buf = (uint8_t*)kmalloc(size);
    if (!buf) return false;

    uint32_t bytes_read = 0;
    uint32_t cur = first_cluster;
    uint32_t bps = fs_bytes_per_sector(fs);
    uint8_t data[ATA_SECTOR_SIZE];
    while (cur >= 2 && cur < 0x0FFFFFF8UL && bytes_read < size) {
        uint32_t dlba = cluster_to_lba(fs, cur);
        for (uint32_t ss = 0; ss < fs->sectors_per_cluster && bytes_read < size; ss++) {
            if (!ata_read512(dlba + ss, data)) { kfree(buf); return false; }

            uint32_t to_copy = bps;
            if (to_copy > (size - bytes_read)) to_copy = size - bytes_read;
            memcpy(buf + bytes_read, data, to_copy);
            bytes_read += to_copy;
        }

        uint32_t next = 0;
        if (!fat32_read_fat_entry(fs, cur, &next)) { kfree(buf); return false; }
        cur = next;
    }

    if (bytes_read != size) {
        kfree(buf);
        return false;
    }

    *out_buf = buf;
    *out_size = size;
    return true;
}

bool fat32_mount(fat32_fs_t* fs, uint32_t part_lba_start) {
    if (!fs) return false;
    memset(fs, 0, sizeof(*fs));

    uint8_t vbr[ATA_SECTOR_SIZE];
    if (!ata_read512(part_lba_start, vbr)) {
        // kprintf("FAT32: failed to read VBR.\n");
        return false;
    }
    if (vbr[510] != 0x55 || vbr[511] != 0xAA) {
        // kprintf("FAT32: bad VBR signature.\n");
        return false;
    }

    // Avoid packed-struct deref here; on some builds this was producing
    // garbage values (and in turn made /user mount fail).
    uint16_t bytes_per_sector = rd16le(vbr + 11);
    uint8_t sectors_per_cluster = vbr[13];
    uint16_t reserved_sector_count = rd16le(vbr + 14);
    uint8_t num_fats = vbr[16];
    uint16_t fat_size_16 = rd16le(vbr + 22);
    uint32_t fat_size_32 = rd32le(vbr + 36);
    uint32_t root_cluster = rd32le(vbr + 44);

    if (bytes_per_sector != 512) {
        // kprintf("FAT32: unsupported bytes/sector %u.\n", (unsigned)bytes_per_sector);
        return false;
    }
    if (sectors_per_cluster == 0) {
        // kprintf("FAT32: invalid sectors/cluster 0.\n");
        return false;
    }
    if (reserved_sector_count == 0) {
        // kprintf("FAT32: invalid reserved sectors 0.\n");
        return false;
    }
    if (num_fats == 0) {
        // kprintf("FAT32: invalid num_fats 0.\n");
        return false;
    }
    // FAT32 should have fat_size_16 = 0 and fat_size_32 != 0.
    if (fat_size_16 != 0 || fat_size_32 == 0) {
        // kprintf("FAT32: not FAT32 (fat16sz=%u fat32sz=%u).\n",
        //     (unsigned)fat_size_16, (unsigned)fat_size_32);
        return false;
    }
    if (root_cluster < 2) {
        // kprintf("FAT32: invalid root cluster %u.\n", (unsigned)root_cluster);
        return false;
    }

    fs->mounted = true;
    fs->part_lba_start = part_lba_start;
    fs->bytes_per_sector = bytes_per_sector;
    fs->sectors_per_cluster = sectors_per_cluster;
    fs->reserved_sector_count = reserved_sector_count;
    fs->num_fats = num_fats;
    fs->fat_size_sectors = fat_size_32;
    fs->root_cluster = root_cluster;

    fs->fat_lba = fs->part_lba_start + fs->reserved_sector_count;
    fs->data_lba = fs->fat_lba + fs->num_fats * fs->fat_size_sectors;

    // kprintf("FAT32: mounted (part LBA %d, root cluster %d).\n", (int)fs->part_lba_start, (int)fs->root_cluster);
    return true;
}

bool fat32_read_root_file(const fat32_fs_t* fs, const char* name, uint8_t** out_buf, uint32_t* out_size) {
    if (!fs || !fs->mounted || !name || !out_buf || !out_size) return false;
    *out_buf = 0;
    *out_size = 0;

    char n83[11];
    if (!name_to_83(name, n83)) return false;

    // Walk root directory cluster chain and search dirents.
    uint32_t dir_cluster = fs->root_cluster;
    uint8_t sec[ATA_SECTOR_SIZE];

    while (dir_cluster >= 2 && dir_cluster < 0x0FFFFFF8UL) {
        uint32_t lba0 = cluster_to_lba(fs, dir_cluster);
        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            if (!ata_read512(lba0 + s, sec)) return false;

            for (uint32_t off = 0; off + sizeof(fat_dirent_t) <= ATA_SECTOR_SIZE; off += 32) {
                const fat_dirent_t* de = (const fat_dirent_t*)(sec + off);
                if (de->name[0] == 0x00) {
                    // end of directory
                    return false;
                }
                if (de->name[0] == 0xE5) continue; // deleted
                if (de->attr == 0x0F) continue;    // LFN entry
                if (de->attr & 0x08) continue;     // volume label

                bool match = true;
                for (int i = 0; i < 11; i++) {
                    if ((char)de->name[i] != n83[i]) { match = false; break; }
                }
                if (!match) continue;

                return fat32_read_file_by_dirent(fs, de, out_buf, out_size);
            }
        }

        uint32_t next = 0;
        if (!fat32_read_fat_entry(fs, dir_cluster, &next)) return false;
        dir_cluster = next;
    }

    return false;
}

bool fat32_list_root_long(const fat32_fs_t* fs, fat32_list_lfn_cb_t cb, void* user) {
    if (!fs || !fs->mounted || !cb) return false;

    uint32_t dir_cluster = fs->root_cluster;
    uint8_t sec[ATA_SECTOR_SIZE];
    char lfn[FAT32_LFN_MAX_CHARS];
    int lfn_len = 0;
    bool have_lfn = false;
    uint8_t lfn_chksum = 0;
    char sfn_name[13];

    lfn_reset(lfn, &lfn_len, &have_lfn, &lfn_chksum);

    while (dir_cluster >= 2 && dir_cluster < 0x0FFFFFF8UL) {
        uint32_t lba0 = cluster_to_lba(fs, dir_cluster);
        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            if (!ata_read512(lba0 + s, sec)) return false;

            for (uint32_t off = 0; off + 32 <= ATA_SECTOR_SIZE; off += 32) {
                const uint8_t* ent = sec + off;
                uint8_t first = ent[0];
                uint8_t attr = ent[11];

                if (first == 0x00) {
                    return true;
                }
                if (first == 0xE5) {
                    // deleted entry; reset any pending LFN
                    lfn_reset(lfn, &lfn_len, &have_lfn, &lfn_chksum);
                    continue;
                }

                if (attr == 0x0F) {
                    lfn_consume_entry((const fat_lfn_t*)ent, lfn, &lfn_len, &have_lfn, &lfn_chksum);
                    continue;
                }

                const fat_dirent_t* de = (const fat_dirent_t*)ent;
                if (de->attr & 0x08) {
                    lfn_reset(lfn, &lfn_len, &have_lfn, &lfn_chksum);
                    continue;
                }
                if (de->name[0] == '.') {
                    lfn_reset(lfn, &lfn_len, &have_lfn, &lfn_chksum);
                    continue;
                }

                bool is_dir = (de->attr & 0x10) != 0;

                const char* out_name = 0;
                bool used_lfn = false;
                if (have_lfn) {
                    lfn_finalize(lfn, &lfn_len);
                    // Only trust LFN if it's non-empty; otherwise fall back to SFN.
                    out_name = (lfn_len > 0) ? lfn : 0;
                    used_lfn = (out_name != 0);
                }

                if (!out_name) {
                    name_83_to_string(de->name, sfn_name);
                    out_name = sfn_name;
                }

#if FAT32_DEBUG_LS
                {
                    if (used_lfn) {
                        fat32_dbg_ls_lfn(out_name);
                    } else {
                        fat32_dbg_ls_sfn(out_name, (int)(have_lfn ? 1 : 0), (int)lfn_len);
                    }
                }
#endif

                if (out_name && out_name[0]) {
                    if (!cb(out_name, is_dir, user)) return true;
                }

                // reset after consuming main entry
                lfn_reset(lfn, &lfn_len, &have_lfn, &lfn_chksum);
            }
        }

        uint32_t next = 0;
        if (!fat32_read_fat_entry(fs, dir_cluster, &next)) return false;
        dir_cluster = next;
    }

    return true;
}

bool fat32_list_root_info(const fat32_fs_t* fs, fat32_list_info_cb_t cb, void* user) {
    if (!fs || !fs->mounted || !cb) return false;

    uint32_t dir_cluster = fs->root_cluster;
    uint8_t sec[ATA_SECTOR_SIZE];
    char lfn[FAT32_LFN_MAX_CHARS];
    int lfn_len = 0;
    bool have_lfn = false;
    uint8_t lfn_chksum = 0;
    char sfn_name[13];

    lfn_reset(lfn, &lfn_len, &have_lfn, &lfn_chksum);

    while (dir_cluster >= 2 && dir_cluster < 0x0FFFFFF8UL) {
        uint32_t lba0 = cluster_to_lba(fs, dir_cluster);
        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            if (!ata_read512(lba0 + s, sec)) return false;

            for (uint32_t off = 0; off + 32 <= ATA_SECTOR_SIZE; off += 32) {
                const uint8_t* ent = sec + off;
                uint8_t first = ent[0];
                uint8_t attr = ent[11];

                if (first == 0x00) return true;
                if (first == 0xE5) {
                    lfn_reset(lfn, &lfn_len, &have_lfn, &lfn_chksum);
                    continue;
                }
                if (attr == 0x0F) {
                    lfn_consume_entry((const fat_lfn_t*)ent, lfn, &lfn_len, &have_lfn, &lfn_chksum);
                    continue;
                }

                const fat_dirent_t* de = (const fat_dirent_t*)ent;
                if (de->attr & 0x08) { lfn_reset(lfn, &lfn_len, &have_lfn, &lfn_chksum); continue; }
                if (de->name[0] == '.') { lfn_reset(lfn, &lfn_len, &have_lfn, &lfn_chksum); continue; }

                bool is_dir = (de->attr & 0x10) != 0;
                uint32_t file_size = is_dir ? 0 : de->file_size;

                const char* out_name = 0;
                if (have_lfn) {
                    lfn_finalize(lfn, &lfn_len);
                    out_name = (lfn_len > 0) ? lfn : 0;
                }
                if (!out_name) {
                    name_83_to_string(de->name, sfn_name);
                    out_name = sfn_name;
                }

                if (out_name && out_name[0]) {
                    if (!cb(out_name, is_dir, file_size, user)) return true;
                }

                lfn_reset(lfn, &lfn_len, &have_lfn, &lfn_chksum);
            }
        }

        uint32_t next = 0;
        if (!fat32_read_fat_entry(fs, dir_cluster, &next)) return false;
        dir_cluster = next;
    }

    return true;
}

bool fat32_read_root_file_long(const fat32_fs_t* fs, const char* name, uint8_t** out_buf, uint32_t* out_size) {
    if (!fs || !fs->mounted || !name || !out_buf || !out_size) return false;
    *out_buf = 0;
    *out_size = 0;

    // First try LFN scan.
    uint32_t dir_cluster = fs->root_cluster;
    uint8_t sec[ATA_SECTOR_SIZE];
    char lfn[FAT32_LFN_MAX_CHARS];
    int lfn_len = 0;
    bool have_lfn = false;
    uint8_t lfn_chksum = 0;

    lfn_reset(lfn, &lfn_len, &have_lfn, &lfn_chksum);

    while (dir_cluster >= 2 && dir_cluster < 0x0FFFFFF8UL) {
        uint32_t lba0 = cluster_to_lba(fs, dir_cluster);
        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            if (!ata_read512(lba0 + s, sec)) return false;

            for (uint32_t off = 0; off + 32 <= ATA_SECTOR_SIZE; off += 32) {
                const uint8_t* ent = sec + off;
                uint8_t first = ent[0];
                uint8_t attr = ent[11];

                if (first == 0x00) {
                    // end; if we didn't find via LFN, try 8.3 fallback.
                    goto fallback_sfn;
                }
                if (first == 0xE5) {
                    lfn_reset(lfn, &lfn_len, &have_lfn, &lfn_chksum);
                    continue;
                }

                if (attr == 0x0F) {
                    lfn_consume_entry((const fat_lfn_t*)ent, lfn, &lfn_len, &have_lfn, &lfn_chksum);
                    continue;
                }

                const fat_dirent_t* de = (const fat_dirent_t*)ent;
                if (de->attr & 0x08) {
                    lfn_reset(lfn, &lfn_len, &have_lfn, &lfn_chksum);
                    continue;
                }

                if (have_lfn) {
                    lfn_finalize(lfn, &lfn_len);
                    if (strcmp(lfn, name) == 0) {
                        return fat32_read_file_by_dirent(fs, de, out_buf, out_size);
                    }
                }

                lfn_reset(lfn, &lfn_len, &have_lfn, &lfn_chksum);
            }
        }

        uint32_t next = 0;
        if (!fat32_read_fat_entry(fs, dir_cluster, &next)) return false;
        dir_cluster = next;
    }

fallback_sfn:
    // Fall back to old 8.3 behavior.
    return fat32_read_root_file(fs, name, out_buf, out_size);
}

bool fat32_list_root(const fat32_fs_t* fs, fat32_list_cb_t cb, void* user) {
    if (!fs || !fs->mounted || !cb) return false;

    uint32_t dir_cluster = fs->root_cluster;
    uint8_t sec[ATA_SECTOR_SIZE];
    char namebuf[13];

    while (dir_cluster >= 2 && dir_cluster < 0x0FFFFFF8UL) {
        uint32_t lba0 = cluster_to_lba(fs, dir_cluster);
        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            if (!ata_read512(lba0 + s, sec)) return false;

            for (uint32_t off = 0; off + sizeof(fat_dirent_t) <= ATA_SECTOR_SIZE; off += 32) {
                const fat_dirent_t* de = (const fat_dirent_t*)(sec + off);
                if (de->name[0] == 0x00) {
                    // End of directory
                    return true;
                }
                if (de->name[0] == 0xE5) continue; // deleted
                if (de->attr == 0x0F) continue;    // LFN entry
                if (de->attr & 0x08) continue;     // volume label

                // Skip '.' and '..'
                if (de->name[0] == '.') continue;

                bool is_dir = (de->attr & 0x10) != 0;
                name_83_to_string(de->name, namebuf);
                if (namebuf[0] == 0) continue;

                if (!cb(namebuf, is_dir, user)) return true;
            }
        }

        uint32_t next = 0;
        if (!fat32_read_fat_entry(fs, dir_cluster, &next)) return false;
        dir_cluster = next;
    }

    return true;
}

// ---------------------------------------------------------------------------
// FAT32 write support
// ---------------------------------------------------------------------------

static bool fat32_write_fat_entry(fat32_fs_t* fs, uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->fat_lba + (fat_offset / ATA_SECTOR_SIZE);
    uint32_t ent_off    = fat_offset % ATA_SECTOR_SIZE;

    uint8_t sec[ATA_SECTOR_SIZE];
    if (!ata_read512(fat_sector, sec)) return false;

    uint32_t old_val = 0;
    memcpy(&old_val, sec + ent_off, 4);
    uint32_t new_val = (old_val & 0xF0000000UL) | (value & 0x0FFFFFFFUL);
    memcpy(sec + ent_off, &new_val, 4);
    if (!ata_write512(fat_sector, sec)) return false;

    if (fs->num_fats >= 2) {
        uint32_t fat2_sector = fat_sector + fs->fat_size_sectors;
        uint8_t  sec2[ATA_SECTOR_SIZE];
        if (ata_read512(fat2_sector, sec2)) {
            old_val = 0;
            memcpy(&old_val, sec2 + ent_off, 4);
            new_val = (old_val & 0xF0000000UL) | (value & 0x0FFFFFFFUL);
            memcpy(sec2 + ent_off, &new_val, 4);
            ata_write512(fat2_sector, sec2);
        }
    }
    return true;
}

static uint32_t fat32_alloc_cluster(fat32_fs_t* fs) {
    for (uint32_t s = 0; s < fs->fat_size_sectors; s++) {
        uint8_t sec[ATA_SECTOR_SIZE];
        if (!ata_read512(fs->fat_lba + s, sec)) continue;

        for (uint32_t off = 0; off + 4 <= ATA_SECTOR_SIZE; off += 4) {
            uint32_t cluster = s * (ATA_SECTOR_SIZE / 4) + (off / 4);
            if (cluster < 2) continue;
            uint32_t val = 0;
            memcpy(&val, sec + off, 4);
            val &= 0x0FFFFFFFUL;
            if (val == 0x00000000UL) {
                if (fat32_write_fat_entry(fs, cluster, 0x0FFFFFF8UL)) return cluster;
                return 0;
            }
        }
    }
    return 0;
}

static void fat32_free_cluster_chain(fat32_fs_t* fs, uint32_t first_cluster) {
    uint32_t cur = first_cluster;
    while (cur >= 2 && cur < 0x0FFFFFF8UL) {
        uint32_t next = 0;
        fat32_read_fat_entry(fs, cur, &next);
        fat32_write_fat_entry(fs, cur, 0x00000000UL);
        cur = next;
    }
}

static uint32_t fat32_write_data_new_chain(fat32_fs_t* fs,
                                            const uint8_t* data, uint32_t size) {
    if (size == 0) return 0;
    uint32_t first_cluster = 0, prev_cluster = 0, offset = 0;
    uint32_t bpc = (uint32_t)fs->sectors_per_cluster * ATA_SECTOR_SIZE;

    while (offset < size) {
        uint32_t cluster = fat32_alloc_cluster(fs);
        if (!cluster) { if (first_cluster) fat32_free_cluster_chain(fs, first_cluster); return 0; }
        if (!first_cluster) first_cluster = cluster;
        if (prev_cluster) fat32_write_fat_entry(fs, prev_cluster, cluster);

        uint32_t lba = cluster_to_lba(fs, cluster);
        for (uint32_t s = 0; s < (uint32_t)fs->sectors_per_cluster; s++) {
            uint8_t sector[ATA_SECTOR_SIZE];
            memset(sector, 0, ATA_SECTOR_SIZE);
            uint32_t pos = offset + s * ATA_SECTOR_SIZE;
            if (pos < size) {
                uint32_t chunk = ATA_SECTOR_SIZE;
                if (pos + chunk > size) chunk = size - pos;
                memcpy(sector, data + pos, chunk);
            }
            if (!ata_write512(lba + s, sector)) {
                if (first_cluster) fat32_free_cluster_chain(fs, first_cluster);
                return 0;
            }
        }
        offset += bpc;
        prev_cluster = cluster;
    }
    return first_cluster;
}

// Compute the FAT LFN checksum over the 11-byte padded 8.3 name.
static uint8_t lfn_name_chksum(const char n83[11]) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + (uint8_t)n83[i]);
    return sum;
}

// Build one 32-byte VFAT LFN entry.
// ord:    1-based ordinal; caller sets 0x40 on the LAST (highest ordinal) entry.
// base:   first character index in `name` this entry covers = (ord & 0x1F - 1) * 13.
// chksum: result of lfn_name_chksum() for the companion SFN.
static void lfn_build_entry(uint8_t out[32], uint8_t ord,
                             const char* name, int namelen,
                             int base, uint8_t chksum) {
    for (int i = 0; i < 32; i++) out[i] = 0xFF; // default padding
    out[0]  = ord;
    out[11] = 0x0F; // LFN attribute
    out[12] = 0x00; // type = 0
    out[13] = chksum;
    out[26] = 0x00; out[27] = 0x00; // cluster = 0

    // Byte offsets of the 13 UTF-16LE code units within the 32-byte entry
    static const int cu_off[13] = {1,3,5,7,9, 14,16,18,20,22,24, 28,30};
    for (int i = 0; i < 13; i++) {
        int pos = base + i;
        uint16_t cu;
        if (pos < namelen)        cu = (uint16_t)(unsigned char)name[pos];
        else if (pos == namelen)  cu = 0x0000; // NUL terminator
        else                      cu = 0xFFFF; // padding
        out[cu_off[i]]     = (uint8_t)(cu & 0xFF);
        out[cu_off[i] + 1] = (uint8_t)(cu >> 8);
    }
}

// Maximum LFN entries for a 255-char name = ceil(255/13) = 20; +1 SFN = 21 slots.
#define FAT_WIN_MAX 22

bool fat32_write_root_file(fat32_fs_t* fs, const char* name,
                            const uint8_t* data, uint32_t size) {
    if (!fs || !fs->mounted || !name) return false;

    char n83[11];
    if (!name_to_83(name, n83)) return false;

    int namelen = 0; while (name[namelen]) namelen++;
    int nlfn   = (namelen + 12) / 13; // LFN entries needed (ceiling div)
    int nslots = nlfn + 1;            // LFN entries + 1 SFN entry
    uint8_t chksum = lfn_name_chksum(n83);

    // Sliding window: track the last nslots consecutive free/deleted directory slots.
    uint32_t wlba[FAT_WIN_MAX];
    uint32_t woff[FAT_WIN_MAX];
    int wfill = 0;
    bool found_run   = false;
    bool found_match = false;
    uint32_t match_lba = 0, match_off = 0, old_cluster = 0;

    uint32_t dir_cluster = fs->root_cluster;
    while (dir_cluster >= 2 && dir_cluster < 0x0FFFFFF8UL) {
        uint32_t lba0 = cluster_to_lba(fs, dir_cluster);
        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            uint32_t lba = lba0 + s;
            uint8_t sec[ATA_SECTOR_SIZE];
            if (!ata_read512(lba, sec)) return false;

            for (uint32_t off = 0; off + 32 <= ATA_SECTOR_SIZE; off += 32) {
                uint8_t first = sec[off];
                uint8_t attr  = sec[off + 11];
                bool is_free  = (first == 0xE5 || first == 0x00);

                // Maintain the free-slot window
                if (!found_run) {
                    if (is_free) {
                        wlba[wfill] = lba;
                        woff[wfill] = off;
                        wfill++;
                        if (wfill == nslots) found_run = true;
                    } else {
                        wfill = 0; // non-free entry breaks the run
                    }
                }

                if (first == 0x00) {
                    // All entries from here on are also 0x00 (free).
                    // Stop only once the window is full; otherwise keep iterating
                    // so subsequent 0x00 entries can fill the remaining slots.
                    if (found_run) goto w_scan_done;
                    continue;
                }

                // Check for existing SFN match (overwrite case)
                if (!found_match && !is_free && attr != 0x0F && !(attr & 0x08)) {
                    bool match = true;
                    for (int i = 0; i < 11; i++) {
                        if (sec[off + (uint32_t)i] != (uint8_t)n83[i]) { match = false; break; }
                    }
                    if (match) {
                        found_match = true;
                        match_lba   = lba;
                        match_off   = off;
                        old_cluster =
                            ((uint32_t)(uint16_t)(sec[off+20] | ((uint16_t)sec[off+21] << 8)) << 16) |
                             (uint32_t)(uint16_t)(sec[off+26] | ((uint16_t)sec[off+27] << 8));
                    }
                }
            }
        }
        uint32_t next_cl = 0;
        if (!fat32_read_fat_entry(fs, dir_cluster, &next_cl)) return false;
        dir_cluster = next_cl;
    }
w_scan_done:
    if (!found_match && !found_run) return false;

    // Write file data to a new cluster chain
    uint32_t new_cluster = fat32_write_data_new_chain(fs, data, size);
    if (size > 0 && new_cluster == 0) return false;

    if (found_match) {
        // Overwrite: update cluster pointer and size in the existing SFN in-place.
        // Any LFN entries before it stay valid since the filename hasn't changed.
        if (old_cluster >= 2) fat32_free_cluster_chain(fs, old_cluster);
        uint8_t sec[ATA_SECTOR_SIZE];
        if (!ata_read512(match_lba, sec)) return false;
        uint16_t hi = (uint16_t)(new_cluster >> 16);
        uint16_t lo = (uint16_t)(new_cluster & 0xFFFF);
        sec[match_off+20] = (uint8_t)(hi & 0xFF); sec[match_off+21] = (uint8_t)(hi >> 8);
        sec[match_off+26] = (uint8_t)(lo & 0xFF); sec[match_off+27] = (uint8_t)(lo >> 8);
        sec[match_off+28] = (uint8_t)(size);        sec[match_off+29] = (uint8_t)(size >> 8);
        sec[match_off+30] = (uint8_t)(size >> 16);  sec[match_off+31] = (uint8_t)(size >> 24);
        return ata_write512(match_lba, sec);
    }

    // Create: write LFN entries (descending ordinal) then the SFN entry.
    // wlba[0]      = slot for highest-ordinal LFN entry (has 0x40 flag set)
    // wlba[nlfn-1] = slot for ordinal-1 LFN entry
    // wlba[nlfn]   = slot for SFN entry
    for (int i = 0; i < nlfn; i++) {
        int     actual_ord = nlfn - i;              // descending: nlfn, nlfn-1, ..., 1
        uint8_t ord        = (uint8_t)actual_ord;
        if (i == 0) ord |= 0x40;                    // mark last (highest) LFN entry
        int base = (actual_ord - 1) * 13;

        uint8_t lfn_buf[32];
        lfn_build_entry(lfn_buf, ord, name, namelen, base, chksum);

        uint8_t sec[ATA_SECTOR_SIZE];
        if (!ata_read512(wlba[i], sec)) return false;
        memcpy(sec + woff[i], lfn_buf, 32);
        if (!ata_write512(wlba[i], sec)) return false;
    }

    // SFN entry
    uint8_t sec[ATA_SECTOR_SIZE];
    if (!ata_read512(wlba[nlfn], sec)) return false;
    uint8_t* de = sec + woff[nlfn];
    memset(de, 0, 32);
    memcpy(de, n83, 11);
    de[11] = 0x20; // archive attribute
    uint16_t hi = (uint16_t)(new_cluster >> 16);
    uint16_t lo = (uint16_t)(new_cluster & 0xFFFF);
    de[20] = (uint8_t)(hi & 0xFF); de[21] = (uint8_t)(hi >> 8);
    de[26] = (uint8_t)(lo & 0xFF); de[27] = (uint8_t)(lo >> 8);
    de[28] = (uint8_t)(size);       de[29] = (uint8_t)(size >> 8);
    de[30] = (uint8_t)(size >> 16); de[31] = (uint8_t)(size >> 24);
    return ata_write512(wlba[nlfn], sec);
}

// Delete a file (or empty directory) from the root directory by its displayed name.
// Marks the SFN and any preceding LFN entries as 0xE5 and frees the cluster chain.
bool fat32_delete_root_file(fat32_fs_t* fs, const char* name) {
    if (!fs || !fs->mounted || !name) return false;

    uint32_t dir_cluster = fs->root_cluster;
    uint8_t sec[ATA_SECTOR_SIZE];
    char lfn_buf[FAT32_LFN_MAX_CHARS];
    int lfn_len = 0;
    bool have_lfn = false;
    uint8_t lfn_chksum_val = 0;

    // Track LFN entry positions so we can mark them deleted alongside the SFN.
    #define DEL_LFN_MAX 22
    uint32_t lfn_lba[DEL_LFN_MAX];
    uint32_t lfn_off_arr[DEL_LFN_MAX];
    int lfn_count = 0;

    lfn_reset(lfn_buf, &lfn_len, &have_lfn, &lfn_chksum_val);

    while (dir_cluster >= 2 && dir_cluster < 0x0FFFFFF8UL) {
        uint32_t lba0 = cluster_to_lba(fs, dir_cluster);
        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            uint32_t lba = lba0 + s;
            if (!ata_read512(lba, sec)) return false;

            for (uint32_t off = 0; off + 32 <= ATA_SECTOR_SIZE; off += 32) {
                uint8_t first = sec[off];
                uint8_t attr  = sec[off + 11];

                if (first == 0x00) return false; // end of directory — not found
                if (first == 0xE5) {
                    lfn_count = 0;
                    lfn_reset(lfn_buf, &lfn_len, &have_lfn, &lfn_chksum_val);
                    continue;
                }

                if (attr == 0x0F) { // LFN entry
                    if (lfn_count < DEL_LFN_MAX) {
                        lfn_lba[lfn_count]     = lba;
                        lfn_off_arr[lfn_count] = off;
                        lfn_count++;
                    }
                    lfn_consume_entry((const fat_lfn_t*)(sec + off), lfn_buf, &lfn_len, &have_lfn, &lfn_chksum_val);
                    continue;
                }

                const fat_dirent_t* de = (const fat_dirent_t*)(sec + off);
                if ((de->attr & 0x08) || de->name[0] == '.') {
                    lfn_count = 0;
                    lfn_reset(lfn_buf, &lfn_len, &have_lfn, &lfn_chksum_val);
                    continue;
                }

                // Determine the display name
                char sfn_name[13];
                const char* cmp_name = 0;
                if (have_lfn) {
                    lfn_finalize(lfn_buf, &lfn_len);
                    if (lfn_len > 0) cmp_name = lfn_buf;
                }
                if (!cmp_name) {
                    name_83_to_string(de->name, sfn_name);
                    cmp_name = sfn_name;
                }

                if (strcmp(cmp_name, name) == 0) {
                    // Extract first cluster of the file/directory
                    uint32_t file_cluster =
                        ((uint32_t)(sec[off+20] | ((uint32_t)sec[off+21] << 8)) << 16) |
                         (uint32_t)(sec[off+26] | ((uint32_t)sec[off+27] << 8));

                    // Mark the SFN entry deleted
                    sec[off] = 0xE5;
                    if (!ata_write512(lba, sec)) return false;

                    // Mark any preceding LFN entries deleted
                    for (int i = 0; i < lfn_count; i++) {
                        uint8_t lsec[ATA_SECTOR_SIZE];
                        if (ata_read512(lfn_lba[i], lsec)) {
                            lsec[lfn_off_arr[i]] = 0xE5;
                            ata_write512(lfn_lba[i], lsec);
                        }
                    }

                    // Free the cluster chain
                    if (file_cluster >= 2 && file_cluster < 0x0FFFFFF8UL)
                        fat32_free_cluster_chain(fs, file_cluster);

                    return true;
                }

                lfn_count = 0;
                lfn_reset(lfn_buf, &lfn_len, &have_lfn, &lfn_chksum_val);
            }
        }
        uint32_t next = 0;
        if (!fat32_read_fat_entry(fs, dir_cluster, &next)) return false;
        dir_cluster = next;
    }
    return false; // not found
}

// Create a directory in the FAT32 root directory.
// Returns false if the name already exists or there is no space.
bool fat32_mkdir_root(fat32_fs_t* fs, const char* name) {
    if (!fs || !fs->mounted || !name) return false;

    char n83[11];
    if (!name_to_83(name, n83)) return false;

    int namelen = 0; while (name[namelen]) namelen++;
    int nlfn    = (namelen + 12) / 13;
    int nslots  = nlfn + 1;
    uint8_t chksum = lfn_name_chksum(n83);

    uint32_t wlba[FAT_WIN_MAX];
    uint32_t woff[FAT_WIN_MAX];
    int wfill = 0;
    bool found_run = false, already_exists = false;

    uint32_t dir_cluster = fs->root_cluster;
    while (dir_cluster >= 2 && dir_cluster < 0x0FFFFFF8UL) {
        uint32_t lba0 = cluster_to_lba(fs, dir_cluster);
        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            uint32_t lba = lba0 + s;
            uint8_t sec[ATA_SECTOR_SIZE];
            if (!ata_read512(lba, sec)) return false;

            for (uint32_t off = 0; off + 32 <= ATA_SECTOR_SIZE; off += 32) {
                uint8_t first = sec[off];
                uint8_t attr  = sec[off + 11];
                bool is_free  = (first == 0xE5 || first == 0x00);

                if (!found_run) {
                    if (is_free) {
                        wlba[wfill] = lba; woff[wfill] = off; wfill++;
                        if (wfill == nslots) found_run = true;
                    } else {
                        wfill = 0;
                    }
                }

                if (first == 0x00) { if (found_run) goto mkdir_done; continue; }
                if (is_free || attr == 0x0F || (sec[off+11] & 0x08)) continue;

                // Check for existing SFN match (already exists)
                bool match = true;
                for (int i = 0; i < 11; i++) {
                    if (sec[off + (uint32_t)i] != (uint8_t)n83[i]) { match = false; break; }
                }
                if (match) { already_exists = true; goto mkdir_done; }
            }
        }
        uint32_t next = 0;
        if (!fat32_read_fat_entry(fs, dir_cluster, &next)) return false;
        dir_cluster = next;
    }
mkdir_done:
    if (already_exists || !found_run) return false;

    // Allocate a cluster for the new directory
    uint32_t new_cluster = fat32_alloc_cluster(fs);
    if (!new_cluster) return false;

    // Zero the cluster then write '.' and '..' entries in its first sector
    uint8_t init[ATA_SECTOR_SIZE];
    memset(init, 0, ATA_SECTOR_SIZE);
    uint32_t new_lba0 = cluster_to_lba(fs, new_cluster);
    for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
        if (!ata_write512(new_lba0 + s, init)) {
            fat32_free_cluster_chain(fs, new_cluster); return false;
        }
    }
    {
        uint8_t sec[ATA_SECTOR_SIZE];
        if (!ata_read512(new_lba0, sec)) { fat32_free_cluster_chain(fs, new_cluster); return false; }
        // '.' entry (offset 0)
        memset(sec, ' ', 11); sec[0] = '.'; sec[11] = 0x10;
        sec[20] = (uint8_t)(new_cluster >> 16); sec[21] = (uint8_t)(new_cluster >> 24);
        sec[26] = (uint8_t)(new_cluster & 0xFF); sec[27] = (uint8_t)((new_cluster >> 8) & 0xFF);
        // '..' entry (offset 32)
        memset(sec + 32, ' ', 11); sec[32] = '.'; sec[33] = '.'; sec[43] = 0x10;
        sec[52] = (uint8_t)(fs->root_cluster >> 16); sec[53] = (uint8_t)(fs->root_cluster >> 24);
        sec[58] = (uint8_t)(fs->root_cluster & 0xFF); sec[59] = (uint8_t)((fs->root_cluster >> 8) & 0xFF);
        if (!ata_write512(new_lba0, sec)) { fat32_free_cluster_chain(fs, new_cluster); return false; }
    }

    // Write LFN entries
    for (int i = 0; i < nlfn; i++) {
        int actual_ord = nlfn - i;
        uint8_t ord = (uint8_t)actual_ord;
        if (i == 0) ord |= 0x40;
        int base = (actual_ord - 1) * 13;
        uint8_t lfn_buf[32];
        lfn_build_entry(lfn_buf, ord, name, namelen, base, chksum);
        uint8_t sec[ATA_SECTOR_SIZE];
        if (!ata_read512(wlba[i], sec)) { fat32_free_cluster_chain(fs, new_cluster); return false; }
        memcpy(sec + woff[i], lfn_buf, 32);
        if (!ata_write512(wlba[i], sec)) { fat32_free_cluster_chain(fs, new_cluster); return false; }
    }

    // Write SFN directory entry
    {
        uint8_t sec[ATA_SECTOR_SIZE];
        if (!ata_read512(wlba[nlfn], sec)) { fat32_free_cluster_chain(fs, new_cluster); return false; }
        uint8_t* de = sec + woff[nlfn];
        memset(de, 0, 32);
        memcpy(de, n83, 11);
        de[11] = 0x10; // ATTR_DIRECTORY
        de[20] = (uint8_t)(new_cluster >> 16); de[21] = (uint8_t)((new_cluster >> 16) >> 8);
        de[26] = (uint8_t)(new_cluster & 0xFF); de[27] = (uint8_t)((new_cluster >> 8) & 0xFF);
        // size = 0 for directory
        if (!ata_write512(wlba[nlfn], sec)) { fat32_free_cluster_chain(fs, new_cluster); return false; }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Attribute get / set
// ---------------------------------------------------------------------------

// Shared: scan root directory for a file by display name; returns the sector
// LBA and byte offset of its SFN entry on success.
static bool fat32_find_sfn(const fat32_fs_t* fs, const char* name,
                            uint32_t* out_lba, uint32_t* out_off) {
    uint32_t dir_cluster = fs->root_cluster;
    uint8_t sec[ATA_SECTOR_SIZE];
    char lfn_buf[FAT32_LFN_MAX_CHARS];
    int lfn_len = 0; bool have_lfn = false; uint8_t lchk = 0;
    lfn_reset(lfn_buf, &lfn_len, &have_lfn, &lchk);

    while (dir_cluster >= 2 && dir_cluster < 0x0FFFFFF8UL) {
        uint32_t lba0 = cluster_to_lba(fs, dir_cluster);
        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            uint32_t lba = lba0 + s;
            if (!ata_read512(lba, sec)) return false;
            for (uint32_t off = 0; off + 32 <= ATA_SECTOR_SIZE; off += 32) {
                uint8_t first = sec[off];
                uint8_t attr  = sec[off + 11];
                if (first == 0x00) return false;
                if (first == 0xE5) { lfn_reset(lfn_buf,&lfn_len,&have_lfn,&lchk); continue; }
                if (attr == 0x0F) {
                    lfn_consume_entry((const fat_lfn_t*)(sec+off),lfn_buf,&lfn_len,&have_lfn,&lchk);
                    continue;
                }
                const fat_dirent_t* de = (const fat_dirent_t*)(sec + off);
                if ((de->attr & 0x08) || de->name[0] == '.') {
                    lfn_reset(lfn_buf,&lfn_len,&have_lfn,&lchk); continue;
                }
                char sfn_name[13]; const char* cmp = 0;
                if (have_lfn) { lfn_finalize(lfn_buf,&lfn_len); if (lfn_len>0) cmp=lfn_buf; }
                if (!cmp) { name_83_to_string(de->name, sfn_name); cmp = sfn_name; }
                if (strcmp(cmp, name) == 0) { *out_lba = lba; *out_off = off; return true; }
                lfn_reset(lfn_buf,&lfn_len,&have_lfn,&lchk);
            }
        }
        uint32_t next = 0;
        if (!fat32_read_fat_entry(fs, dir_cluster, &next)) return false;
        dir_cluster = next;
    }
    return false;
}

bool fat32_get_root_attr(const fat32_fs_t* fs, const char* name, uint8_t* out_attr) {
    if (!fs || !fs->mounted || !name || !out_attr) return false;
    uint32_t lba, off;
    if (!fat32_find_sfn(fs, name, &lba, &off)) return false;
    uint8_t sec[ATA_SECTOR_SIZE];
    if (!ata_read512(lba, sec)) return false;
    *out_attr = sec[off + 11];
    return true;
}

bool fat32_set_root_attr(fat32_fs_t* fs, const char* name, uint8_t new_attr) {
    if (!fs || !fs->mounted || !name) return false;
    uint32_t lba, off;
    if (!fat32_find_sfn(fs, name, &lba, &off)) return false;
    uint8_t sec[ATA_SECTOR_SIZE];
    if (!ata_read512(lba, sec)) return false;
    // Preserve directory (0x10) and volume-label (0x08) bits.
    new_attr = (new_attr & ~0x18u) | (sec[off + 11] & 0x18u);
    sec[off + 11] = new_attr;
    return ata_write512(lba, sec);
}
