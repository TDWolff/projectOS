#ifndef MBR_GPT_H
#define MBR_GPT_H

#include "../../include/types.h"

// Minimal MBR/GPT helpers.
// We only need: detect a protective MBR and parse GPT partition entries.

typedef struct {
    uint64_t first_lba;
    uint64_t last_lba;
    uint8_t type_guid[16];
    uint8_t part_guid[16];
    char name_utf16le[72]; // 36 UTF-16 code units
} gpt_partition_t;

// Scan the disk for a GPT partition that contains a FAT32 filesystem.
// Returns true and fills out_start_lba/out_last_lba on success.
bool gpt_find_fat32_partition(uint64_t* out_start_lba, uint64_t* out_last_lba);

#endif
