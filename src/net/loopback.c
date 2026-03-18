#include "loopback.h"

#include "net.h"

#include "../lib/string.h"
#include "../mem/heap.h"

// Forward declare net core
void net_handle_packet(void* packet, uint16_t packet_length, net_nic_interfaces_t* nic);

static uint8_t g_lo_mac[6] = {0, 0, 0, 0, 0, 0};

net_nic_interfaces_t nic_loopback = {
    .name = "lo",
    .flags = IFF_UP | IFF_RUNNING | IFF_LOOPBACK,
    .type = INTERFACE_ETH | INTERFACE_LOOPBACK,
    .mtu = 65536,
    .ip_address = {127, 0, 0, 1},
    .subnet = {255, 0, 0, 1},
    .gateway = {0, 0, 0, 0},
    .get_mac_addr = 0,
    .send_packet = 0,
};

static uint8_t* loopback_get_mac_addr(void) {
    return g_lo_mac;
}

static void loopback_send_packet(uint8_t* dest, void* payload, uint32_t payload_len, uint16_t ethertype) {
    (void)dest;

    // Best-effort TX stats.
    nic_loopback.tx_packets++;
    nic_loopback.tx_bytes += (uint64_t)(sizeof(network_packet_t) + payload_len);

    // Re-wrap payload in an Ethernet frame and feed it back into the stack.
    uint32_t frame_len = (uint32_t)sizeof(network_packet_t) + payload_len;
    network_packet_t* frame = (network_packet_t*)kmalloc(frame_len);
    if (!frame) return;

    memset(frame->destination_mac, 0, 6);
    memset(frame->source_mac, 0, 6);
    frame->type = BSWAP16(ethertype);
    memcpy(frame->data, payload, payload_len);

    net_handle_packet(frame, (uint16_t)frame_len, &nic_loopback);

    kfree(frame);
}

void loopback_init(void) {
    nic_loopback.get_mac_addr = loopback_get_mac_addr;
    nic_loopback.send_packet = loopback_send_packet;
}
