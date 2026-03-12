#include "fat32.h"

#include "../../drivers/block/ata_pio.h"
#include "../../lib/stdio.h"
#include "../../lib/string.h"
#include "../../mem/heap.h"

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
    // Fill with spaces.
    for (int i = 0; i < 11; i++) out11[i] = ' ';

    // Copy name up to '.', max 8.
    int i = 0;
    int j = 0;
    while (in[i] && in[i] != '.' && j < 8) {
        char c = in[i++];
        if (c == '/') return false;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        out11[j++] = c;
    }

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

bool fat32_mount(fat32_fs_t* fs, uint32_t part_lba_start) {
    if (!fs) return false;
    memset(fs, 0, sizeof(*fs));

    uint8_t vbr[ATA_SECTOR_SIZE];
    if (!ata_read512(part_lba_start, vbr)) {
        kprintf("FAT32: failed to read VBR.\n");
        return false;
    }
    if (vbr[510] != 0x55 || vbr[511] != 0xAA) {
        kprintf("FAT32: bad VBR signature.\n");
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
        kprintf("FAT32: unsupported bytes/sector %u.\n", (unsigned)bytes_per_sector);
        return false;
    }
    if (sectors_per_cluster == 0) {
        kprintf("FAT32: invalid sectors/cluster 0.\n");
        return false;
    }
    if (reserved_sector_count == 0) {
        kprintf("FAT32: invalid reserved sectors 0.\n");
        return false;
    }
    if (num_fats == 0) {
        kprintf("FAT32: invalid num_fats 0.\n");
        return false;
    }
    // FAT32 should have fat_size_16 = 0 and fat_size_32 != 0.
    if (fat_size_16 != 0 || fat_size_32 == 0) {
        kprintf("FAT32: not FAT32 (fat16sz=%u fat32sz=%u).\n",
            (unsigned)fat_size_16, (unsigned)fat_size_32);
        return false;
    }
    if (root_cluster < 2) {
        kprintf("FAT32: invalid root cluster %u.\n", (unsigned)root_cluster);
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

    kprintf("FAT32: mounted (part LBA %d, root cluster %d).\n", (int)fs->part_lba_start, (int)fs->root_cluster);
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
        }

        uint32_t next = 0;
        if (!fat32_read_fat_entry(fs, dir_cluster, &next)) return false;
        dir_cluster = next;
    }

    return false;
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
