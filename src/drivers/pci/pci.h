#ifndef PCI_H
#define PCI_H

#include "../../include/types.h"

// Classic PCI config space access (I/O ports 0xCF8/0xCFC).
// Enough to enumerate QEMU's e1000.

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t header_type;

    uint8_t irq_line;

    uint32_t bar[6];
} pci_device_t;

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

bool pci_get_device(uint8_t bus, uint8_t slot, uint8_t func, pci_device_t* out);

// Scan all buses/slots/functions and call cb for each present function.
// Return false from cb to stop scanning.

typedef bool (*pci_scan_cb_t)(const pci_device_t* dev, void* user);
void pci_scan(pci_scan_cb_t cb, void* user);

// Enable PCI bus master + memory/io space as appropriate.
void pci_enable_bus_master(const pci_device_t* dev);

#endif
