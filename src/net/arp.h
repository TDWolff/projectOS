#ifndef ARP_H
#define ARP_H

#include "net.h"

typedef struct __attribute__((packed)) {
    uint16_t hw_type;
    uint16_t protocol;
    uint8_t hw_addr_len;
    uint8_t protocol_addr_len;
    uint16_t opcode;
    uint8_t source_mac[6];
    uint8_t source_protocol_addr[4];
    uint8_t destination_mac[6];
    uint8_t destination_protocol_addr[4];
} arp_packet_t;

void arp_init(void);
void arp_handle(arp_packet_t* packet, uint32_t length, net_nic_interfaces_t* nic);

// Query the ARP table for an IP. Returns true on hit.
bool arp_resolve(uint8_t ip[4], uint8_t out_mac[6]);

// Broadcast an ARP request for `ip`.
void arp_lookup(uint8_t ip[4], net_nic_interfaces_t* nic);

#endif
