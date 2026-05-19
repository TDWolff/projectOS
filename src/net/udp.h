#ifndef UDP_H
#define UDP_H

#include "ip.h"
#include "net.h"

typedef struct __attribute__((packed)) {
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t length;
    uint16_t checksum;
    uint8_t data[];
} udp_packet_t;

uint16_t udp_calculate_checksum(void* addr, int count);

void udp_handle(ip_packet_t* packet, uint32_t length, uint8_t dest_mac[6], net_nic_interfaces_t* nic);

void udp_send(ip_packet_t* packet, uint16_t length, uint8_t destination_protocol_addr[4],
              uint8_t dest_mac[6], uint16_t dest_port, uint16_t source_port,
              net_nic_interfaces_t* nic);

#endif
