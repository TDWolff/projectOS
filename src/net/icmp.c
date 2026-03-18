#include "icmp.h"

#include "../lib/string.h"

static struct {
    bool seen;
    uint16_t id_be;
    uint16_t seq_be;
} g_icmp_selftest = {0};

void icmp_selftest_reset(void) {
    g_icmp_selftest.seen = false;
    g_icmp_selftest.id_be = 0;
    g_icmp_selftest.seq_be = 0;
}

void icmp_observe_for_selftest(uint16_t identifier_be, uint16_t sequence_be) {
    g_icmp_selftest.seen = true;
    g_icmp_selftest.id_be = identifier_be;
    g_icmp_selftest.seq_be = sequence_be;
}

bool icmp_selftest_wait_for_echo_reply(uint16_t identifier_be, uint16_t sequence_be, uint32_t spin_iters) {
    for (volatile uint32_t i = 0; i < spin_iters; i++) {
        if (g_icmp_selftest.seen && g_icmp_selftest.id_be == identifier_be && g_icmp_selftest.seq_be == sequence_be) {
            return true;
        }
        __asm__ volatile("pause");
    }
    return false;
}

void icmp_echo_reply(ip_packet_t* ip_pack, uint32_t length, uint8_t dest_mac[6], net_nic_interfaces_t* nic) {
    if (!ip_pack || !nic) return;

    uint8_t source_protocol_addr[4] = {0};
    memcpy(source_protocol_addr, ip_pack->source_protocol_addr, 4);

    icmp_header_t* icmp_pack = (icmp_header_t*)ip_pack->data;

    // Echo reply? (useful for self-tests)
    if (icmp_pack->type == 0) {
        // Make sure we can read identifier/sequence.
        uint32_t ihl_bytes = (uint32_t)ip_pack->internet_header_length * 4u;
        if (length >= ihl_bytes + sizeof(icmp_header_t) + sizeof(icmp_echo_hdr_t)) {
            icmp_echo_hdr_t* eh = (icmp_echo_hdr_t*)icmp_pack->data;
            icmp_observe_for_selftest(eh->identifier, eh->sequence);
        }
        return;
    }

    // Echo request?
    if (icmp_pack->type != 8) return;

    icmp_pack->type = 0;
    icmp_pack->checksum = 0;
    icmp_pack->checksum = ip_calculate_checksum(
        icmp_pack, (int)(length - ip_pack->internet_header_length * 4));

    ip_send(ip_pack, (uint16_t)length, source_protocol_addr, dest_mac, nic);
}
