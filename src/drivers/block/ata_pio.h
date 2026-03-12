#ifndef ATA_PIO_H
#define ATA_PIO_H

#include "../../include/types.h"

// Minimal ATA PIO (LBA28) driver for QEMU IDE disks.
// Primary master at 0x1F0.

#define ATA_SECTOR_SIZE 512

typedef struct {
    bool present;
    uint32_t sector_size;
    uint32_t sectors_per_request; // currently 1
    uint32_t lba28_max;           // max LBA addressable (inclusive)
} ata_pio_device_t;

// Probe primary-master device. Returns true if present.
bool ata_pio_init();

const ata_pio_device_t* ata_pio_get_device();

// Read/write a single 512-byte sector.
// Returns true on success.
bool ata_pio_read_sector(uint32_t lba, void* out512);
bool ata_pio_write_sector(uint32_t lba, const void* in512);

#endif
