#include "mbr_gpt.h"

#include "../../drivers/block/ata_pio.h"
#include "../../lib/stdio.h"
#include "../../lib/string.h"

// GPT structures (on-disk). Only fields we actually use.
#pragma pack(push, 1)
typedef struct {
    uint8_t boot_indicator;
    uint8_t start_chs[3];
    uint8_t partition_type;
    uint8_t end_chs[3];
    uint32_t start_lba;
    uint32_t sectors;
} mbr_part_entry_t;

typedef struct {
    uint8_t boot_code[440];
    uint32_t disk_signature;
    uint16_t reserved;
    mbr_part_entry_t part[4];
    uint16_t signature;
} mbr_t;

typedef struct {
    uint64_t signature;           // "EFI PART" 0x5452415020494645
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t part_entries_lba;
    uint32_t num_part_entries;
    uint32_t part_entry_size;
    uint32_t part_entries_crc32;
    // rest ignored
} gpt_header_t;

typedef struct {
    uint8_t type_guid[16];
    uint8_t part_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attrs;
    uint16_t name_utf16le[36];
} gpt_entry_t;
#pragma pack(pop)

static bool is_zero_guid_16(const uint8_t g[16]) {
    for (int i = 0; i < 16; i++) {
        if (g[i] != 0) return false;
    }
    return true;
}

// Note: We intentionally do NOT require a particular GPT type GUID.
// Different host tools use different type GUIDs (e.g. EFI System, basic data,
// Linux filesystem) for FAT32. We'll instead *probe* the VBR for FAT32.

static bool read_lba(uint32_t lba28, void* out512) {
    return ata_pio_read_sector(lba28, out512);
}

bool gpt_find_fat32_partition(uint64_t* out_start_lba, uint64_t* out_last_lba) {
    if (!out_start_lba || !out_last_lba) return false;

    uint8_t sec0[ATA_SECTOR_SIZE];
    if (!read_lba(0, sec0)) {
        kprintf("GPT: failed to read LBA0.\n");
        return false;
    }

    const mbr_t* mbr = (const mbr_t*)sec0;
    if (mbr->signature != 0xAA55) {
        kprintf("GPT: invalid MBR signature.\n");
        return false;
    }

    bool protective = false;
    for (int i = 0; i < 4; i++) {
        if (mbr->part[i].partition_type == 0xEE) {
            protective = true;
            break;
        }
    }
    if (!protective) {
        kprintf("GPT: no protective MBR found.\n");
        return false;
    }

    uint8_t sec1[ATA_SECTOR_SIZE];
    if (!read_lba(1, sec1)) {
        kprintf("GPT: failed to read GPT header (LBA1).\n");
        return false;
    }

    const gpt_header_t* hdr = (const gpt_header_t*)sec1;
    if (hdr->signature != 0x5452415020494645ULL) {
        kprintf("GPT: bad signature.\n");
        return false;
    }
    if (hdr->part_entry_size < sizeof(gpt_entry_t) || hdr->part_entry_size > 512) {
        kprintf("GPT: unsupported entry size %u.\n", (unsigned)hdr->part_entry_size);
        return false;
    }
    if (hdr->num_part_entries == 0 || hdr->num_part_entries > 256) {
        kprintf("GPT: suspicious entry count %u.\n", (unsigned)hdr->num_part_entries);
        return false;
    }

    // Iterate partition entries; we read sectors lazily.
    uint32_t entries_lba = (uint32_t)hdr->part_entries_lba;
    uint32_t entry_size = hdr->part_entry_size;
    uint32_t entries_per_sector = ATA_SECTOR_SIZE / entry_size;
    if (entries_per_sector == 0) return false;

    uint8_t entsec[ATA_SECTOR_SIZE];
    for (uint32_t idx = 0; idx < hdr->num_part_entries; idx++) {
        uint32_t sec_index = idx / entries_per_sector;
        uint32_t off = (idx % entries_per_sector) * entry_size;

        if ((idx % entries_per_sector) == 0) {
            if (!read_lba(entries_lba + sec_index, entsec)) {
                kprintf("GPT: failed to read entries sector %u.\n", (unsigned)sec_index);
                return false;
            }
        }

        const gpt_entry_t* ent = (const gpt_entry_t*)(entsec + off);
        if (is_zero_guid_16(ent->type_guid)) continue;

    // Validate FAT32 by peeking at the VBR signature and BPB values.
        if (ent->first_lba > 0x0FFFFFFFULL) continue; // keep within LBA28
        uint8_t vbr[ATA_SECTOR_SIZE];
        if (!read_lba((uint32_t)ent->first_lba, vbr)) {
            continue;
        }
        if (vbr[510] != 0x55 || vbr[511] != 0xAA) continue;
    // Don't require the "FAT32" label (not mandatory). We accept this entry
    // as a candidate and let fat32_mount() do strict BPB validation.

        *out_start_lba = ent->first_lba;
        *out_last_lba = ent->last_lba;
        kprintf("GPT: found candidate FAT32 partition at LBA %d.\n", (int)(uint32_t)ent->first_lba);
        return true;
    }

    kprintf("GPT: no FAT32 partition found.\n");
    return false;
}
