#include "e1000.h"

#include "../pci/pci.h"
#include "../../lib/string.h"
#include "../../lib/stdio.h"
#include "../../mem/vmm.h"
#include "../../mem/dma.h"

#include "../../net/net.h"

#include "../../mem/heap.h"

// --- e1000 register offsets (MMIO) ----------------------------------------

#define E1000_REG_CTRL   0x0000
#define E1000_REG_STATUS 0x0008
#define E1000_REG_EERD   0x0014
#define E1000_REG_RAL    0x5400
#define E1000_REG_RAH    0x5404

// Interrupt cause (we won't use IRQs yet, but reading clears pending bits)
#define E1000_REG_ICR    0x00C0

// TX
#define E1000_REG_TDBAL  0x3800
#define E1000_REG_TDBAH  0x3804
#define E1000_REG_TDLEN  0x3808
#define E1000_REG_TDH    0x3810
#define E1000_REG_TDT    0x3818
#define E1000_REG_TCTL   0x0400
#define E1000_REG_TIPG   0x0410

// RX
#define E1000_REG_RDBAL  0x2800
#define E1000_REG_RDBAH  0x2804
#define E1000_REG_RDLEN  0x2808
#define E1000_REG_RDH    0x2810
#define E1000_REG_RDT    0x2818
#define E1000_REG_RCTL   0x0100

// --- e1000 descriptor formats ---------------------------------------------

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} e1000_tx_desc_t;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} e1000_rx_desc_t;

// TX cmd bits
#define E1000_TX_CMD_EOP (1u << 0)
#define E1000_TX_CMD_IFCS (1u << 1)
#define E1000_TX_CMD_RS  (1u << 3)

// TX status bits
#define E1000_TX_STATUS_DD (1u << 0)

// RX status bits
#define E1000_RX_STATUS_DD  (1u << 0)
#define E1000_RX_STATUS_EOP (1u << 1)

// TCTL
#define E1000_TCTL_EN   (1u << 1)
#define E1000_TCTL_PSP  (1u << 3)

// RCTL
#define E1000_RCTL_EN   (1u << 1)
#define E1000_RCTL_SBP  (1u << 2)
#define E1000_RCTL_UPE  (1u << 3)
#define E1000_RCTL_MPE  (1u << 4)
#define E1000_RCTL_BAM  (1u << 15)
#define E1000_RCTL_BSIZE_2048 (0u << 16)
#define E1000_RCTL_SECRC (1u << 26)

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

// Rings
#define E1000_TX_RING_SIZE 64
#define E1000_RX_RING_SIZE 128

static e1000_tx_desc_t* g_tx_desc = 0;
static e1000_rx_desc_t* g_rx_desc = 0;

static uint8_t* g_tx_buf[E1000_TX_RING_SIZE];
static uint8_t* g_rx_buf[E1000_RX_RING_SIZE];

static uint32_t g_tx_tail = 0;
static uint32_t g_rx_tail = 0;

static volatile uint64_t g_e1000_tx_drops = 0;

static inline void mmio_write32(uint32_t reg, uint32_t v) {
    g_e1000_mmio[reg / 4] = v;
}

static inline uint32_t mmio_read32(uint32_t reg) {
    return g_e1000_mmio[reg / 4];
}

static inline void e1000_flush(void) {
    // Any register read forces a flush on some platforms.
    (void)mmio_read32(E1000_REG_STATUS);
}

static uint8_t* e1000_get_mac_addr(void) {
    return g_e1000_mac;
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
    .send_packet = 0, // set after function definition
};

static void e1000_send_packet(uint8_t* dest_mac, void* payload, uint32_t payload_len, uint16_t ethertype) {
    if (!g_e1000_mmio || !g_tx_desc) return;
    if (!payload || payload_len == 0) return;

    // Build an Ethernet frame into a staging buffer owned by the TX slot.
    uint32_t frame_len = (uint32_t)sizeof(network_packet_t) + payload_len;
    if (frame_len > 1518) {
        // Don't support jumbo frames.
        return;
    }

    uint32_t idx = g_tx_tail;
    e1000_tx_desc_t* d = &g_tx_desc[idx];

    // If the descriptor isn't done, ring is full.
    if ((d->status & E1000_TX_STATUS_DD) == 0) {
    g_e1000_tx_drops++;
        return;
    }

    uint8_t* buf = g_tx_buf[idx];
    if (!buf) return;

    network_packet_t* eth = (network_packet_t*)buf;
    if (dest_mac) memcpy(eth->destination_mac, dest_mac, 6);
    else memset(eth->destination_mac, 0xFF, 6); // broadcast default
    memcpy(eth->source_mac, g_e1000_mac, 6);
    eth->type = BSWAP16(ethertype);
    memcpy(eth->data, payload, payload_len);

    d->addr = (uint64_t)buf;
    d->length = (uint16_t)frame_len;
    d->cso = 0;
    d->cmd = (uint8_t)(E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS);
    d->status = 0;
    d->css = 0;
    d->special = 0;

    // Increment driver stats.
    g_e1000_nic.tx_packets++;
    g_e1000_nic.tx_bytes += frame_len;

    // Advance tail and notify hardware.
    g_tx_tail = (g_tx_tail + 1) % E1000_TX_RING_SIZE;
    mmio_write32(E1000_REG_TDT, g_tx_tail);
    e1000_flush();
}

static void e1000_reset_hw(void) {
    uint32_t ctrl = mmio_read32(E1000_REG_CTRL);
    mmio_write32(E1000_REG_CTRL, ctrl | E1000_CTRL_RST);
    e1000_flush();

    // Poll until the hardware self-clears RST (spec requirement).
    for (volatile uint32_t i = 0; i < 1000000; i++) {
        if (!(mmio_read32(E1000_REG_CTRL) & E1000_CTRL_RST)) break;
        __asm__ volatile("pause");
    }
}

static bool e1000_init_tx(void) {
    // Use a PMM page for DMA safety/alignment.
    void* page = dma_alloc_page();
    if (!page) return false;
    g_tx_desc = (e1000_tx_desc_t*)page;
    memset(g_tx_desc, 0, PAGE_SIZE);

    for (uint32_t i = 0; i < E1000_TX_RING_SIZE; i++) {
    // Back each TX slot with a physical page so the NIC can DMA it.
    g_tx_buf[i] = (uint8_t*)dma_alloc_page();
    if (!g_tx_buf[i]) return false;
        memset(g_tx_buf[i], 0, 2048);
        g_tx_desc[i].addr = (uint64_t)g_tx_buf[i];
        g_tx_desc[i].status = E1000_TX_STATUS_DD;
    }

    uint64_t base = (uint64_t)g_tx_desc;
    mmio_write32(E1000_REG_TDBAL, (uint32_t)(base & 0xFFFFFFFFu));
    mmio_write32(E1000_REG_TDBAH, (uint32_t)(base >> 32));
    mmio_write32(E1000_REG_TDLEN, (uint32_t)(sizeof(e1000_tx_desc_t) * E1000_TX_RING_SIZE));
    mmio_write32(E1000_REG_TDH, 0);
    mmio_write32(E1000_REG_TDT, 0);

    // TCTL: enable + pad short packets.
    // CT=0x10, COLD=0x40 are typical defaults.
    uint32_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP | (0x10u << 4) | (0x40u << 12);
    mmio_write32(E1000_REG_TCTL, tctl);

    // TIPG (inter-packet gap) recommended values.
    mmio_write32(E1000_REG_TIPG, 0x0060200A);

    g_tx_tail = 0;
    return true;
}

static bool e1000_init_rx(void) {
    // Use a PMM page for DMA safety/alignment.
    void* page = dma_alloc_page();
    if (!page) return false;
    g_rx_desc = (e1000_rx_desc_t*)page;
    memset(g_rx_desc, 0, PAGE_SIZE);

    for (uint32_t i = 0; i < E1000_RX_RING_SIZE; i++) {
    // Back each RX slot with a physical page so the NIC can DMA into it.
    g_rx_buf[i] = (uint8_t*)dma_alloc_page();
    if (!g_rx_buf[i]) return false;
        memset(g_rx_buf[i], 0, 2048);
        g_rx_desc[i].addr = (uint64_t)g_rx_buf[i];
        g_rx_desc[i].status = 0;
    }

    uint64_t base = (uint64_t)g_rx_desc;
    mmio_write32(E1000_REG_RDBAL, (uint32_t)(base & 0xFFFFFFFFu));
    mmio_write32(E1000_REG_RDBAH, (uint32_t)(base >> 32));
    mmio_write32(E1000_REG_RDLEN, (uint32_t)(sizeof(e1000_rx_desc_t) * E1000_RX_RING_SIZE));
    mmio_write32(E1000_REG_RDH, 0);
    mmio_write32(E1000_REG_RDT, E1000_RX_RING_SIZE - 1);

    // RCTL: enable, accept broadcast, strip CRC.
    // With RAR0 programmed to our MAC, we shouldn't need promiscuous mode.
    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_BAM |
                    E1000_RCTL_SECRC | E1000_RCTL_BSIZE_2048;
    mmio_write32(E1000_REG_RCTL, rctl);

    g_rx_tail = E1000_RX_RING_SIZE - 1;
    return true;
}

static void e1000_program_rar0(void) {
    // Program Receive Address Register 0 with our MAC and set VALID bit.
    // RAL: low 32 bits of MAC, RAH: high 16 bits plus AV bit (bit 31).
    uint32_t ral = (uint32_t)g_e1000_mac[0] |
                   ((uint32_t)g_e1000_mac[1] << 8) |
                   ((uint32_t)g_e1000_mac[2] << 16) |
                   ((uint32_t)g_e1000_mac[3] << 24);
    uint32_t rah = (uint32_t)g_e1000_mac[4] |
                   ((uint32_t)g_e1000_mac[5] << 8) |
                   (1u << 31);
    mmio_write32(E1000_REG_RAL, ral);
    mmio_write32(E1000_REG_RAH, rah);
    e1000_flush();
}

void e1000_poll(void) {
    if (!g_e1000_mmio || !g_rx_desc) return;

    // Clear any pending interrupt causes (even though IRQs are off).
    (void)mmio_read32(E1000_REG_ICR);

    // Process received packets.
    while (1) {
        uint32_t idx = (g_rx_tail + 1) % E1000_RX_RING_SIZE;
        e1000_rx_desc_t* d = &g_rx_desc[idx];

        if ((d->status & E1000_RX_STATUS_DD) == 0) break;
        if ((d->status & E1000_RX_STATUS_EOP) == 0) {
            // We don't support multi-descriptor packets yet; drop.
            d->status = 0;
            g_rx_tail = idx;
            mmio_write32(E1000_REG_RDT, g_rx_tail);
            continue;
        }

        uint16_t len = d->length;
        if (len >= sizeof(network_packet_t) && len <= 2048) {
            uint8_t* buf = g_rx_buf[idx];
            if (buf) {
                // Update stats before handing into stack.
                g_e1000_nic.rx_packets++;
                g_e1000_nic.rx_bytes += len;
                net_handle_packet(buf, len, &g_e1000_nic);
            }
        }

        // Return descriptor to HW.
        d->status = 0;
        g_rx_tail = idx;
        mmio_write32(E1000_REG_RDT, g_rx_tail);
    }
}

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

    uint64_t mmio_phys;
    if ((bar0 & 0x6) == 0x4) {
        // 64-bit BAR: upper 32 bits live in BAR1.
        mmio_phys = (uint64_t)(bar0 & 0xFFFFFFF0u) | ((uint64_t)dev->bar[1] << 32);
    } else {
        mmio_phys = (uint64_t)(bar0 & 0xFFFFFFF0u);
    }
    if (!mmio_phys) return false;

    // Identity mapping policy note:
    // Your boot code maps a large low region; in QEMU the e1000 MMIO BAR is
    // typically below 4GB, so it should already be accessible. If you later
    // remove identity mapping, map it here with vmm_map_page().

    g_e1000_mmio = (volatile uint32_t*)(mmio_phys);

    // Basic liveness check
    (void)mmio_read32(E1000_REG_STATUS);

    // Read MAC from RAL/RAH before reset, because reset clears those registers.
    if (!e1000_read_mac_from_mmio()) return false;

    // Reset hardware (polls until RST bit self-clears).
    e1000_reset_hw();

    // Reprogram RAR0 with our MAC after reset cleared it.
    e1000_program_rar0();

    // Now that the function exists, wire the callback.
    g_e1000_nic.send_packet = e1000_send_packet;

    if (!e1000_init_tx()) return false;
    if (!e1000_init_rx()) return false;

    // Mark up + running.
    g_e1000_nic.flags = IFF_UP | IFF_RUNNING;

    // Register NIC so it shows up in netif/netdevice.
    (void)net_register_nic(&g_e1000_nic);

    // Re-run discovery now that we have a real NIC.
    net_discovery_run();

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
