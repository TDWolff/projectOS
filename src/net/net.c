#include "net.h"

// include shell.h
#include "../drivers/shell.h"

#include "../lib/string.h"
#include "../mem/heap.h"
#include "../cpu/idt.h"
#include "../fs/initrd.h"
#include "../fs/pfs/pfs.h"

#include "../lib/settings.h"

#include "arp.h"
#include "ip.h"

#include "discovery.h"
#include "loopback.h"
#include "dhcp.h"

// NIC drivers
#include "../drivers/net/e1000.h"

#include "../lib/stdio.h"

// --- NIC registry ----------------------------------------------------------

#define NET_MAX_NICS 8
static net_nic_interfaces_t* g_net_nics[NET_MAX_NICS];
static uint32_t g_net_nic_count = 0;

// --- Debug (bring-up) ------------------------------------------------------
static volatile uint64_t g_net_dbg_rx_frames = 0;
static volatile uint16_t g_net_dbg_last_ethertype = 0;

uint64_t net_dbg_get_rx_frames(void) { return g_net_dbg_rx_frames; }
uint16_t net_dbg_get_last_ethertype(void) { return g_net_dbg_last_ethertype; }
void net_dbg_reset(void) {
    g_net_dbg_rx_frames = 0;
    g_net_dbg_last_ethertype = 0;
}

bool net_register_nic(net_nic_interfaces_t* nic) {
    if (!nic) return false;

    // Don't register duplicates.
    for (uint32_t i = 0; i < g_net_nic_count; i++) {
        if (g_net_nics[i] == nic) return true;
    }

    if (g_net_nic_count >= NET_MAX_NICS) return false;
    g_net_nics[g_net_nic_count++] = nic;
    return true;
}

uint32_t net_get_nic_count(void) {
    return g_net_nic_count;
}

net_nic_interfaces_t* net_get_nic(uint32_t idx) {
    if (idx >= g_net_nic_count) return 0;
    return g_net_nics[idx];
}

void net_handle_packet_deferred(void* packet, uint32_t packet_length, net_nic_interfaces_t* nic) {
    // ProjectOS doesn't have Polaris's full scheduler/thread API exposed here yet.
    // Keep the same conceptual hook point, but call directly for now.
    net_handle_packet(packet, packet_length, nic);
}

void net_handle_packet(void* packet, uint32_t packet_length, net_nic_interfaces_t* nic) {
    if (!packet || !nic) return;
    if (packet_length < sizeof(network_packet_t)) return;

    g_net_dbg_rx_frames++;

    network_packet_t* net_pack = (network_packet_t*)packet;

    void* data = (uint8_t*)packet + sizeof(network_packet_t);
    uint32_t data_length = (uint32_t)packet_length - (uint32_t)sizeof(network_packet_t);

    uint16_t type = BSWAP16(net_pack->type);
    g_net_dbg_last_ethertype = type;
    if (type == REQ_TYPE_ARP) {
        arp_handle((arp_packet_t*)data, data_length, nic);
    } else if (type == REQ_TYPE_IP) {
        ip_handle((ip_packet_t*)data, data_length, net_pack->source_mac, nic);
    } else {
        // kprintf("NET: Unknown ethertype 0x%x\n", type);
    }
}

void net_init(void) {
    // Registry init
    memset(g_net_nics, 0, sizeof(g_net_nics));
    g_net_nic_count = 0;

    // Protocol layers
    arp_init();
    // print to sys terminal saying ok
    kprintf("NET: init ok (loopback up)\n");

    // Devices
    // Loopback registers itself and is always present.
    loopback_init();
    (void)net_register_nic(&nic_loopback);

    // Probe hardware NIC(s)
    (void)e1000_init();

    // Select primary NIC (currently loopback-only, until a real NIC driver registers).
    net_discovery_run();

    // If a real NIC came up, attempt DHCP to get a proper IP/gateway.
    net_nic_interfaces_t* primary = net_get_primary_nic();
    if (primary && !(primary->flags & IFF_LOOPBACK)) {
        dhcp_request_lease(primary);
        // Re-run discovery in case DHCP changed the NIC state.
        net_discovery_run();
    }
}
