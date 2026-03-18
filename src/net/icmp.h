#ifndef ICMP_H
#define ICMP_H

#include "ip.h"
#include "net.h"

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint8_t data[];
} icmp_header_t;

void icmp_echo_reply(ip_packet_t* ip_pack, uint32_t length, uint8_t dest_mac[6], net_nic_interfaces_t* nic);

#endif
