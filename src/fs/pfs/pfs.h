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

#endif
