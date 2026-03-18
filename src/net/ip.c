#include "ip.h"

#include "icmp.h"
#include "udp.h"

#include "../lib/string.h"
#include "../mem/heap.h"

uint16_t ip_calculate_checksum(void* addr, int count) {
    // RFC1071
    uint32_t sum = 0;
    uint16_t* ptr = (uint16_t*)addr;

    while (count > 1) {
        sum += *ptr++;
        count -= 2;
    }

    if (count > 0) sum += *(uint8_t*)ptr;

    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);

    return (uint16_t)(~sum);
}

void ip_send(ip_packet_t* packet, uint16_t length, uint8_t destination_protocol_addr[4],
             uint8_t dest_mac[6], net_nic_interfaces_t* nic) {
    if (!packet || !nic || !nic->send_packet) return;

    packet->version = 4;
    packet->internet_header_length = 5;
    packet->length = BSWAP16(length);
    packet->id = BSWAP16(packet->id);
    packet->fragment_offset = BSWAP16(0x4000);
    packet->time_to_live = 64;

    packet->checksum = 0;

    memcpy(packet->destination_protocol_addr, destination_protocol_addr, 4);
    memcpy(packet->source_protocol_addr, nic->ip_address, 4);

    packet->checksum = ip_calculate_checksum(packet, (int)sizeof(ip_packet_t));

    nic->send_packet(dest_mac, packet, length, REQ_TYPE_IP);
}

void ip_handle(ip_packet_t* packet, uint32_t length, uint8_t dest_mac[6], net_nic_interfaces_t* nic) {
    if (!packet || !nic) return;

    if (packet->protocol == 1) {
        // ICMP
        void* clone = 0;
        clone = kmalloc(length);
        if (!clone) return;
        memcpy(clone, packet, length);

        icmp_echo_reply((ip_packet_t*)clone, length, dest_mac, nic);
        kfree(clone);
    } else if (packet->protocol == 17) {
        udp_handle(packet, length, dest_mac, nic);
    }
}
