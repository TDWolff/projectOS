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

// ICMP echo header (first 4 bytes of data for echo request/reply).
typedef struct __attribute__((packed)) {
    uint16_t identifier;
    uint16_t sequence;
} icmp_echo_hdr_t;

void icmp_echo_reply(ip_packet_t* ip_pack, uint32_t length, uint8_t dest_mac[6], net_nic_interfaces_t* nic);

// Self-test/diagnostic helpers: latch when we observe an echo reply.
void icmp_observe_for_selftest(uint16_t identifier_be, uint16_t sequence_be);
bool icmp_selftest_wait_for_echo_reply(uint16_t identifier_be, uint16_t sequence_be, uint32_t spin_iters);
void icmp_selftest_reset(void);

#endif
