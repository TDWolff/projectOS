#ifndef PFS_H
#define PFS_H

#include "../../include/types.h"

// PFS (Project File System)
// Phase-1 minimal scaffolding:
// - initrd remains the system (/) source
// - /user is backed by a persistent block device (later FAT32)
// For now, we only provide a persistence smoke-test API.

typedef struct {
    bool has_disk;
    bool user_mounted;
} pfs_state_t;

void pfs_init();
const pfs_state_t* pfs_get_state();

// Debug helper: Re-run disk + mount probing and print verbose info via kprintf.
// This is meant to be invoked from the interactive shell so output is visible
// in the windowed terminal (scrollback supported).
typedef void (*pfs_putc_fn)(char c, void* user);

// Print verbose probe info using the provided character sink.
// This should be the shell/terminal_window sink, not VGA.
void pfs_debug_probe_and_print_to(pfs_putc_fn putc_cb, void* user);

// Backwards-compatible helper (prints via kprintf/VGA).
void pfs_debug_probe_and_print();

// Writes a signature to disk (if present) and reads it back.
// Returns true if persistence path is functional.
bool pfs_persist_smoketest();

// Read a file from the persistent /user volume.
// Current implementation is FAT32 read-only and only supports root-directory 8.3 names.
// Example path: "/user/settings.pset"
// On success, allocates *out_buf via kmalloc and sets *out_size.
bool pfs_read_user_file(const char* path, uint8_t** out_buf, uint32_t* out_size);

// List the root directory of /user (FAT32, 8.3 names only).
// Callback receives a NUL-terminated name like "FOO.TXT".
typedef bool (*pfs_list_cb_t)(const char* name, bool is_dir, void* user);
bool pfs_list_user_root(pfs_list_cb_t cb, void* user);

// Long-name aware variant.
typedef bool (*pfs_list_lfn_cb_t)(const char* name, bool is_dir, void* user);
bool pfs_list_user_root_long(pfs_list_lfn_cb_t cb, void* user);

// Long-name variant that also passes the file size (0 for directories).
typedef bool (*pfs_list_info_cb_t)(const char* name, bool is_dir, uint32_t size, void* user);
bool pfs_list_user_root_info(pfs_list_info_cb_t cb, void* user);

// Write (create or overwrite) a file on the /user FAT32 volume.
// path must be "/user/filename.ext" or "user/filename.ext" (root dir only).
// filename is stored as an 8.3 uppercase name on disk.
// Returns true on success.
bool pfs_write_user_file(const char* path, const uint8_t* data, uint32_t size);

// Delete a file from /user. Returns false if not found.
bool pfs_delete_user_file(const char* path);

// Create a directory directly inside /user. Returns false if it already exists.
bool pfs_mkdir_user(const char* path);

// Get/set the FAT attribute byte for a /user file.
// Attribute bits: 0x01=Read-Only, 0x02=Hidden, 0x20=Archive.
// Directory (0x10) and volume-label (0x08) bits are preserved automatically by set.
bool pfs_get_user_file_attr(const char* path, uint8_t* out_attr);
bool pfs_set_user_file_attr(const char* path, uint8_t new_attr);

#endif
