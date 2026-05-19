#ifndef FAT32_H
#define FAT32_H

#include "../../include/types.h"

typedef struct {
    bool mounted;
    uint32_t part_lba_start; // start of partition (VBR)

    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t num_fats;
    uint32_t fat_size_sectors;
    uint32_t root_cluster;

    uint32_t fat_lba;
    uint32_t data_lba;
} fat32_fs_t;

bool fat32_mount(fat32_fs_t* fs, uint32_t part_lba_start);

// Read an entire file from the root directory by 8.3 uppercased name.
// Example: "SETTINGS.PSET" (will be converted to 8.3 internally).
// Returns true on success; allocates *out_buf via kmalloc.
bool fat32_read_root_file(const fat32_fs_t* fs, const char* name, uint8_t** out_buf, uint32_t* out_size);

// Iterate root-directory entries (8.3 only; LFN skipped).
// Callback receives a NUL-terminated 8.3 name like "FOO.TXT" and a flag
// indicating if it's a directory. Returning false stops iteration.
typedef bool (*fat32_list_cb_t)(const char* name, bool is_dir, void* user);
bool fat32_list_root(const fat32_fs_t* fs, fat32_list_cb_t cb, void* user);

// Long filename (VFAT) helpers.
// If an LFN exists for an entry, it will be used; otherwise the 8.3 name is used.
// All names passed to callbacks are NUL-terminated.
#define FAT32_LFN_MAX_CHARS 255

// Read an entire file from the root directory by *long* name (case-insensitive ASCII).
// Falls back to 8.3 matching if no LFN entries are present.
bool fat32_read_root_file_long(const fat32_fs_t* fs, const char* name, uint8_t** out_buf, uint32_t* out_size);

// Iterate root-directory entries using long name when present.
typedef bool (*fat32_list_lfn_cb_t)(const char* name, bool is_dir, void* user);
bool fat32_list_root_long(const fat32_fs_t* fs, fat32_list_lfn_cb_t cb, void* user);

// Iterate root-directory entries with file size included in callback.
// Like fat32_list_root_long but passes the on-disk file_size field.
// Directories get size == 0.
typedef bool (*fat32_list_info_cb_t)(const char* name, bool is_dir, uint32_t size, void* user);
bool fat32_list_root_info(const fat32_fs_t* fs, fat32_list_info_cb_t cb, void* user);

// Write (create or overwrite) a file in the root directory using its 8.3 name.
// The name is auto-converted to uppercase 8.3 format.
// Returns true on success.
bool fat32_write_root_file(fat32_fs_t* fs, const char* name,
                           const uint8_t* data, uint32_t size);

#endif
