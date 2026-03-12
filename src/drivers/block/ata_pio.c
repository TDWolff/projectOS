#include "ata_pio.h"

#include "../../include/ports.h"
#include "../../lib/stdio.h"
#include "../../lib/string.h"

// Primary bus I/O base
#define ATA_IO_BASE   0x1F0
#define ATA_CTRL_BASE 0x3F6

// Registers (offsets)
#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x01
#define ATA_REG_FEATURES   0x01
#define ATA_REG_SECCOUNT0  0x02
#define ATA_REG_LBA0       0x03
#define ATA_REG_LBA1       0x04
#define ATA_REG_LBA2       0x05
#define ATA_REG_HDDEVSEL   0x06
#define ATA_REG_COMMAND    0x07
#define ATA_REG_STATUS     0x07

// Control register
#define ATA_REG_CONTROL    0x00 // at CTRL_BASE

// Commands
#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_IDENTIFY      0xEC

// Status bits
#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF   0x20
#define ATA_SR_DSC  0x10
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

static ata_pio_device_t g_dev = {0};

static inline void ata_io_delay() {
    // 400ns delay: read alternate status 4 times.
    (void)inb(ATA_CTRL_BASE);
    (void)inb(ATA_CTRL_BASE);
    (void)inb(ATA_CTRL_BASE);
    (void)inb(ATA_CTRL_BASE);
}

static bool ata_wait_not_bsy(uint32_t timeout) {
    while (timeout--) {
        uint8_t st = inb(ATA_IO_BASE + ATA_REG_STATUS);
        if (!(st & ATA_SR_BSY)) return true;
    }
    return false;
}

static bool ata_wait_drq(uint32_t timeout) {
    while (timeout--) {
        uint8_t st = inb(ATA_IO_BASE + ATA_REG_STATUS);
        if (st & ATA_SR_ERR) return false;
        if (st & ATA_SR_DF) return false;
        if ((st & ATA_SR_BSY) == 0 && (st & ATA_SR_DRQ)) return true;
    }
    return false;
}

static void ata_select_primary_master() {
    // 0xE0: LBA mode, master
    outb(ATA_IO_BASE + ATA_REG_HDDEVSEL, 0xE0);
    ata_io_delay();
}

static void ata_pio_read_data_words(uint16_t* out, int word_count) {
    for (int i = 0; i < word_count; i++) {
        out[i] = inw(ATA_IO_BASE + ATA_REG_DATA);
    }
}

static void ata_pio_write_data_words(const uint16_t* in, int word_count) {
    for (int i = 0; i < word_count; i++) {
        outw(ATA_IO_BASE + ATA_REG_DATA, in[i]);
    }
}

bool ata_pio_init() {
    memset(&g_dev, 0, sizeof(g_dev));

    // Disable interrupts from the device (polling mode)
    outb(ATA_CTRL_BASE + ATA_REG_CONTROL, 0x02);

    ata_select_primary_master();

    // Zero registers per spec before IDENTIFY
    outb(ATA_IO_BASE + ATA_REG_SECCOUNT0, 0);
    outb(ATA_IO_BASE + ATA_REG_LBA0, 0);
    outb(ATA_IO_BASE + ATA_REG_LBA1, 0);
    outb(ATA_IO_BASE + ATA_REG_LBA2, 0);

    outb(ATA_IO_BASE + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_io_delay();

    uint8_t status = inb(ATA_IO_BASE + ATA_REG_STATUS);
    if (status == 0) {
        // No device.
        return false;
    }

    if (!ata_wait_not_bsy(1000000)) {
        return false;
    }

    // If LBA1/LBA2 are non-zero, it's likely ATAPI.
    uint8_t lba1 = inb(ATA_IO_BASE + ATA_REG_LBA1);
    uint8_t lba2 = inb(ATA_IO_BASE + ATA_REG_LBA2);
    if (lba1 != 0 || lba2 != 0) {
        kprintf("ATA: Primary master looks like ATAPI (LBA1=%x LBA2=%x)\n", lba1, lba2);
        return false;
    }

    if (!ata_wait_drq(1000000)) {
        uint8_t err = inb(ATA_IO_BASE + ATA_REG_ERROR);
        kprintf("ATA: IDENTIFY failed (err=%x)\n", err);
        return false;
    }

    uint16_t identify[256];
    ata_pio_read_data_words(identify, 256);

    // words 60-61: total number of user addressable sectors for LBA28
    uint32_t lba28_sectors = (uint32_t)identify[60] | ((uint32_t)identify[61] << 16);

    g_dev.present = true;
    g_dev.sector_size = ATA_SECTOR_SIZE;
    g_dev.sectors_per_request = 1;
    g_dev.lba28_max = (lba28_sectors == 0) ? 0 : (lba28_sectors - 1);

    kprintf("ATA: Primary master present. LBA28 sectors=%d\n", lba28_sectors);

    return true;
}

const ata_pio_device_t* ata_pio_get_device() {
    return &g_dev;
}

static bool ata_pio_do_lba28(uint8_t cmd, uint32_t lba, void* buf512, bool is_write) {
    if (!g_dev.present) return false;
    if (lba > 0x0FFFFFFF) return false;

    // Select drive + top 4 bits of LBA
    outb(ATA_IO_BASE + ATA_REG_HDDEVSEL, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_io_delay();

    // One sector
    outb(ATA_IO_BASE + ATA_REG_SECCOUNT0, 1);
    outb(ATA_IO_BASE + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_IO_BASE + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_IO_BASE + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));

    outb(ATA_IO_BASE + ATA_REG_COMMAND, cmd);
    ata_io_delay();

    if (!ata_wait_drq(1000000)) {
        uint8_t err = inb(ATA_IO_BASE + ATA_REG_ERROR);
        kprintf("ATA: cmd %x DRQ timeout (lba=%x err=%x)\n", cmd, lba, err);
        return false;
    }

    if (is_write) {
        ata_pio_write_data_words((const uint16_t*)buf512, 256);
        // Flush cache (optional). Many emulators ignore; real ATA uses 0xE7.
        // We'll skip for now.
    } else {
        ata_pio_read_data_words((uint16_t*)buf512, 256);
    }

    // Wait for completion
    if (!ata_wait_not_bsy(1000000)) {
        return false;
    }

    uint8_t st = inb(ATA_IO_BASE + ATA_REG_STATUS);
    if (st & ATA_SR_ERR) {
        uint8_t err = inb(ATA_IO_BASE + ATA_REG_ERROR);
        kprintf("ATA: cmd %x failed (lba=%x err=%x)\n", cmd, lba, err);
        return false;
    }

    return true;
}

bool ata_pio_read_sector(uint32_t lba, void* out512) {
    return ata_pio_do_lba28(ATA_CMD_READ_SECTORS, lba, out512, false);
}

bool ata_pio_write_sector(uint32_t lba, const void* in512) {
    // cast away const for common function
    return ata_pio_do_lba28(ATA_CMD_WRITE_SECTORS, lba, (void*)in512, true);
}
