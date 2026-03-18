#include "e1000.h"

#include "../pci/pci.h"
#include "../../lib/string.h"
#include "../../lib/stdio.h"
#include "../../mem/vmm.h"

#include "../../net/net.h"

// --- e1000 register offsets (MMIO) ----------------------------------------

#define E1000_REG_CTRL   0x0000
#define E1000_REG_STATUS 0x0008
#define E1000_REG_EERD   0x0014
#define E1000_REG_RAL    0x5400
#define E1000_REG_RAH    0x5404

// CTRL bits
#define E1000_CTRL_RST (1u << 26)

// EERD bits
#define E1000_EERD_START (1u << 0)
#define E1000_EERD_DONE  (1u << 4)

// Intel vendor
#define PCI_VENDOR_INTEL 0x8086
// QEMU e1000 is often 0x100E, but accept common variants.
static bool is_e1000_device(uint16_t vendor, uint16_t device) {
    if (vendor != PCI_VENDOR_INTEL) return false;
    switch (device) {
        case 0x100E: // 82540EM (QEMU default "e1000")
        case 0x100F:
        case 0x1010:
        case 0x10EA:
            return true;
        default:
            return false;
    }
}

static volatile uint32_t* g_e1000_mmio = 0;
static uint8_t g_e1000_mac[6] = {0};

static inline void mmio_write32(uint32_t reg, uint32_t v) {
    g_e1000_mmio[reg / 4] = v;
}

static inline uint32_t mmio_read32(uint32_t reg) {
    return g_e1000_mmio[reg / 4];
}

static uint8_t* e1000_get_mac_addr(void) {
    return g_e1000_mac;
}

static void e1000_send_packet(uint8_t* dest_mac, void* payload, uint32_t payload_len, uint16_t ethertype) {
    (void)dest_mac;
    (void)payload;
    (void)payload_len;
    (void)ethertype;
    // TX path (descriptor ring) comes next milestone.
}

static net_nic_interfaces_t g_e1000_nic = {
    .name = "e1000",
    .flags = 0,
    .type = INTERFACE_ETH,
    .mtu = 1500,
    .ip_address = {10, 0, 2, 15}, // typical for QEMU user-mode networking (DHCP later)
    .subnet = {255, 255, 255, 0},
    .gateway = {10, 0, 2, 2},
    .get_mac_addr = e1000_get_mac_addr,
    .send_packet = e1000_send_packet,
};

static bool e1000_read_mac_from_mmio(void) {
    uint32_t ral = mmio_read32(E1000_REG_RAL);
    uint32_t rah = mmio_read32(E1000_REG_RAH);

    g_e1000_mac[0] = (uint8_t)(ral & 0xFF);
    g_e1000_mac[1] = (uint8_t)((ral >> 8) & 0xFF);
    g_e1000_mac[2] = (uint8_t)((ral >> 16) & 0xFF);
    g_e1000_mac[3] = (uint8_t)((ral >> 24) & 0xFF);
    g_e1000_mac[4] = (uint8_t)(rah & 0xFF);
    g_e1000_mac[5] = (uint8_t)((rah >> 8) & 0xFF);

    // If RAH "valid" bit isn't set, still keep MAC; QEMU usually sets it.
    return true;
}

static bool e1000_try_init_from_pci(const pci_device_t* dev) {
    // BAR0 is MMIO for QEMU e1000.
    uint32_t bar0 = dev->bar[0];
    if ((bar0 & 0x1) != 0) {
        // I/O BAR (unexpected for QEMU e1000)
        return false;
    }

    uint64_t mmio_phys = (uint64_t)(bar0 & 0xFFFFFFF0u);
    if (!mmio_phys) return false;

    // Identity mapping policy note:
    // Your boot code maps a large low region; in QEMU the e1000 MMIO BAR is
    // typically below 4GB, so it should already be accessible. If you later
    // remove identity mapping, map it here with vmm_map_page().

    g_e1000_mmio = (volatile uint32_t*)(mmio_phys);

    // Basic liveness check
    (void)mmio_read32(E1000_REG_STATUS);

    // Read MAC
    if (!e1000_read_mac_from_mmio()) return false;

    // Mark up + running.
    g_e1000_nic.flags = IFF_UP | IFF_RUNNING;

    // Register NIC so it shows up in netif/netdevice.
    (void)net_register_nic(&g_e1000_nic);

    // Re-run discovery now that we have a real NIC.
    net_discovery_run();

    kprintf("E1000: detected at %x:%x.%x BAR0=%x IRQ=%d\n",
            dev->bus, dev->slot, dev->func, dev->bar[0], dev->irq_line);

    return true;
}

static bool e1000_scan_cb(const pci_device_t* dev, void* user) {
    (void)user;
    if (!is_e1000_device(dev->vendor_id, dev->device_id)) return true;

    pci_enable_bus_master(dev);

    // Stop after first successfully initialized e1000.
    if (e1000_try_init_from_pci(dev)) return false;

    return true;
}

bool e1000_init(void) {
    pci_scan(e1000_scan_cb, 0);
    return g_e1000_mmio != 0;
}
