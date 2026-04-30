#include "udp.h"
#include "dhcp.h"

// Optional self-test hook (defined in net/selftest.c).
// Weak linkage isn't available in our simple build, so we just declare it.
// If you remove the selftest module later, remove this declaration too.
void net_selftest__on_udp_echo(void);

uint16_t udp_calculate_checksum(void* addr, int count) {
    return ip_calculate_checksum(addr, count);
}

void udp_handle(ip_packet_t* packet, uint32_t length, uint8_t dest_mac[6], net_nic_interfaces_t* nic) {

    // Polaris has a tiny echo service on port 7.
    udp_packet_t* udp_pack = (udp_packet_t*)packet->data;

    // Bounds: ensure header fits
    if (length < (uint32_t)(sizeof(ip_packet_t) + sizeof(udp_packet_t))) return;

    if (BSWAP16(udp_pack->destination_port) == 7) {
        net_selftest__on_udp_echo();
        uint16_t temp = udp_pack->destination_port;
        udp_pack->destination_port = udp_pack->source_port;
        udp_pack->source_port = temp;
        ip_send(packet, (uint16_t)length, packet->source_protocol_addr, dest_mac, nic);
    } else if (BSWAP16(udp_pack->destination_port) == 68) {
        // DHCP reply from server
        uint32_t dhcp_len = length > (uint32_t)(sizeof(ip_packet_t) + sizeof(udp_packet_t))
            ? length - (uint32_t)sizeof(ip_packet_t) - (uint32_t)sizeof(udp_packet_t)
            : 0;
        dhcp_handle((dhcp_packet_t*)udp_pack->data, dhcp_len, nic);
    }
}

void udp_send(ip_packet_t* packet, uint16_t length, uint8_t destination_protocol_addr[4],
              uint8_t dest_mac[6], uint16_t dest_port, uint16_t source_port,
              net_nic_interfaces_t* nic) {
    if (!packet) return;

    udp_packet_t* udp_pack = (udp_packet_t*)packet->data;
    udp_pack->source_port = BSWAP16(source_port);
    udp_pack->destination_port = BSWAP16(dest_port);
    udp_pack->length = BSWAP16(length + (uint16_t)sizeof(udp_packet_t));
    udp_pack->checksum = 0;

    packet->protocol = 17;

    ip_send(packet,
            (uint16_t)(length + sizeof(udp_packet_t) + sizeof(ip_packet_t)),
            destination_protocol_addr, dest_mac, nic);
}
