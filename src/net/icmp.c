#include "icmp.h"

#include "../lib/string.h"

void icmp_echo_reply(ip_packet_t* ip_pack, uint32_t length, uint8_t dest_mac[6], net_nic_interfaces_t* nic) {
    if (!ip_pack || !nic) return;

    uint8_t source_protocol_addr[4] = {0};
    memcpy(source_protocol_addr, ip_pack->source_protocol_addr, 4);

    icmp_header_t* icmp_pack = (icmp_header_t*)ip_pack->data;

    // Echo request?
    if (icmp_pack->type != 8) return;

    icmp_pack->type = 0;
    icmp_pack->checksum = 0;
    icmp_pack->checksum = ip_calculate_checksum(
        icmp_pack, (int)(length - ip_pack->internet_header_length * 4));

    ip_send(ip_pack, (uint16_t)length, source_protocol_addr, dest_mac, nic);
}
