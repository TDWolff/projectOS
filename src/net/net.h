#ifndef NET_H
#define NET_H

#include "../include/types.h"

// Endianness helpers (Polaris-style)
#define BSWAP16(x) ((((x) & 0xff) << 8) | (((x) & 0xff00) >> 8))
#define BSWAP32(x)                                                       \
    ((((x) & 0x000000ff) << 24) | (((x) & 0x0000ff00) << 8) |            \
     (((x) & 0x00ff0000) >> 8) | (((x) & 0xff000000) >> 24))

// Ethernet types
#define REQ_TYPE_ARP 0x0806
#define REQ_TYPE_IP  0x0800

// Interface types / flags (tiny subset)
#define INTERFACE_ETH      0
#define INTERFACE_LOOPBACK 1

#define IFF_UP       (1u << 0)
#define IFF_LOOPBACK (1u << 3)
#define IFF_RUNNING  (1u << 6)

// Ethernet frame wrapper used internally.
// Packet layout: dst mac (6), src mac (6), ethertype (2), payload...
typedef struct __attribute__((packed)) {
    uint8_t destination_mac[6];
    uint8_t source_mac[6];
    uint16_t type;
    uint8_t data[];
} network_packet_t;

// NIC interface exposed to the network stack.
// Hardware drivers fill these callbacks.
typedef struct net_nic_interfaces {
    char name[16];
    uint32_t flags;
    uint32_t type;
    uint32_t mtu;

    // Optional stats (best-effort).
    // Drivers can increment these when sending/receiving.
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;

    uint8_t ip_address[4];
    uint8_t subnet[4];
    uint8_t gateway[4];
    uint8_t dns_server[4];  // from DHCP option 6; all-zero = not set

    uint8_t* (*get_mac_addr)(void);
    void (*send_packet)(uint8_t* dest_mac, void* payload, uint32_t payload_len, uint16_t ethertype);
} net_nic_interfaces_t;

// NIC registry (so the shell can list interfaces, and drivers can register).
// Returns true on success, false if the registry is full or nic is NULL.
bool net_register_nic(net_nic_interfaces_t* nic);

// Get registered NIC count (including loopback).
uint32_t net_get_nic_count(void);

// Get NIC by index [0..count-1]. Returns NULL if out of range.
net_nic_interfaces_t* net_get_nic(uint32_t idx);

// Run simple NIC discovery/selection.
// Picks a primary NIC for the stack (first non-loopback usable NIC), else loopback.
void net_discovery_run(void);

// Get the currently-selected primary NIC.
// Never returns NULL.
net_nic_interfaces_t* net_get_primary_nic(void);

// Returns true if discovery selected a primary NIC (may still be loopback).
bool net_has_primary_nic(void);

// Initialize networking stack + built-in devices (loopback).
void net_init(void);

// Core receive entrypoint: hardware drivers should call this with an Ethernet frame.
void net_handle_packet(void* packet, uint32_t packet_length, net_nic_interfaces_t* nic);

// Bring-up debug helpers
uint64_t net_dbg_get_rx_frames(void);
uint16_t net_dbg_get_last_ethertype(void);
void net_dbg_reset(void);

// Optional helper: deliver a packet in a task context. If you don't have a thread
// system like Polaris, you can just call net_handle_packet directly.
void net_handle_packet_deferred(void* packet, uint32_t packet_length, net_nic_interfaces_t* nic);

#endif
