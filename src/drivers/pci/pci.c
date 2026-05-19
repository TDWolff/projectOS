#include "pci.h"

#include "../../include/ports.h"
#include "../../lib/string.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static uint32_t pci_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (1u << 31) |
           ((uint32_t)bus << 16) |
           ((uint32_t)slot << 11) |
           ((uint32_t)func << 8) |
           ((uint32_t)(offset & 0xFC));
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_addr(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_addr(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

bool pci_get_device(uint8_t bus, uint8_t slot, uint8_t func, pci_device_t* out) {
    if (!out) return false;

    uint32_t id = pci_read32(bus, slot, func, 0x00);
    uint16_t vendor = (uint16_t)(id & 0xFFFF);
    if (vendor == 0xFFFF) return false;

    memset(out, 0, sizeof(*out));
    out->bus = bus;
    out->slot = slot;
    out->func = func;
    out->vendor_id = vendor;
    out->device_id = (uint16_t)(id >> 16);

    uint32_t class_reg = pci_read32(bus, slot, func, 0x08);
    out->prog_if = (uint8_t)((class_reg >> 8) & 0xFF);
    out->subclass = (uint8_t)((class_reg >> 16) & 0xFF);
    out->class_code = (uint8_t)((class_reg >> 24) & 0xFF);

    uint32_t hdr = pci_read32(bus, slot, func, 0x0C);
    out->header_type = (uint8_t)((hdr >> 16) & 0xFF);

    for (int i = 0; i < 6; i++) {
        out->bar[i] = pci_read32(bus, slot, func, (uint8_t)(0x10 + i * 4));
    }

    uint32_t irq = pci_read32(bus, slot, func, 0x3C);
    out->irq_line = (uint8_t)(irq & 0xFF);

    return true;
}

static bool pci_scan_one_bus(uint8_t bus, pci_scan_cb_t cb, void* user) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        // func 0 tells us if device exists and if it's multifunction.
        pci_device_t dev0;
        if (!pci_get_device(bus, slot, 0, &dev0)) continue;

        if (!cb(&dev0, user)) return false;

        bool multi = (dev0.header_type & 0x80) != 0;
        uint8_t funcs = multi ? 8 : 1;
        for (uint8_t func = 1; func < funcs; func++) {
            pci_device_t dev;
            if (!pci_get_device(bus, slot, func, &dev)) continue;
            if (!cb(&dev, user)) return false;
        }
    }
    return true;
}

void pci_scan(pci_scan_cb_t cb, void* user) {
    if (!cb) return;
    for (uint16_t bus = 0; bus < 256; bus++) {
        if (!pci_scan_one_bus((uint8_t)bus, cb, user)) return;
    }
}

void pci_enable_bus_master(const pci_device_t* dev) {
    if (!dev) return;

    // Command register at 0x04 (16-bit):
    // bit0 IO space, bit1 memory space, bit2 bus master.
    uint32_t cmdsts = pci_read32(dev->bus, dev->slot, dev->func, 0x04);
    uint16_t cmd = (uint16_t)(cmdsts & 0xFFFF);
    cmd |= (1u << 2); // bus master
    cmd |= (1u << 1); // memory space
    cmd |= (1u << 0); // io space (harmless if unused)
    cmdsts = (cmdsts & 0xFFFF0000u) | cmd;
    pci_write32(dev->bus, dev->slot, dev->func, 0x04, cmdsts);
}
