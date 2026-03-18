#include "arp.h"

#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../mem/heap.h"

// Extremely small ARP table (enough for bring-up).
typedef struct {
    uint8_t ip[4];
    uint8_t mac[6];
    bool used;
} arp_entry_t;

#define ARP_TABLE_MAX 16
static arp_entry_t g_arp_table[ARP_TABLE_MAX];

static uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

// Polaris hard-codes my_ip for bring-up. We'll do the same unless a NIC driver
// overwrites nic->ip_address.
static uint8_t g_default_ip[4] = {192, 168, 10, 10};

static arp_entry_t* arp_find_entry(uint8_t ip[4]) {
    for (int i = 0; i < ARP_TABLE_MAX; i++) {
        if (g_arp_table[i].used && memcmp(g_arp_table[i].ip, ip, 4) == 0) {
            return &g_arp_table[i];
        }
    }
    return 0;
}

static void arp_put_entry(uint8_t ip[4], uint8_t mac[6]) {
    arp_entry_t* e = arp_find_entry(ip);
    if (!e) {
        for (int i = 0; i < ARP_TABLE_MAX; i++) {
            if (!g_arp_table[i].used) {
                e = &g_arp_table[i];
                e->used = true;
                break;
            }
        }
    }
    if (!e) return;

    memcpy(e->ip, ip, 4);
    memcpy(e->mac, mac, 6);
}

bool arp_resolve(uint8_t ip[4], uint8_t out_mac[6]) {
    if (!ip || !out_mac) return false;
    arp_entry_t* e = arp_find_entry(ip);
    if (!e) return false;
    memcpy(out_mac, e->mac, 6);
    return true;
}

void arp_init(void) {
    memset(g_arp_table, 0, sizeof(g_arp_table));
}

static void arp_send(arp_packet_t* packet, uint32_t length, net_nic_interfaces_t* nic) {
    (void)length;

    memcpy(packet->source_mac, nic->get_mac_addr ? nic->get_mac_addr() : broadcast_mac, 6);

    // if nic ip not set, use default bring-up ip
    bool ip_zero = true;
    for (int i = 0; i < 4; i++) if (nic->ip_address[i] != 0) ip_zero = false;
    if (ip_zero) memcpy(nic->ip_address, g_default_ip, 4);

    memcpy(packet->source_protocol_addr, nic->ip_address, 4);

    packet->opcode = BSWAP16(1); // request
    packet->hw_addr_len = 6;
    packet->protocol_addr_len = 4;
    packet->hw_type = BSWAP16(1); // ethernet
    packet->protocol = BSWAP16(REQ_TYPE_IP);

    if (nic->send_packet) {
        nic->send_packet(packet->destination_mac, packet, sizeof(arp_packet_t), REQ_TYPE_ARP);
    }
}

void arp_lookup(uint8_t ip[4], net_nic_interfaces_t* nic) {
    if (!nic) return;

    arp_packet_t* p = (arp_packet_t*)kmalloc(sizeof(arp_packet_t));
    if (!p) return;
    memset(p, 0, sizeof(*p));

    memcpy(p->destination_protocol_addr, ip, 4);
    memcpy(p->destination_mac, broadcast_mac, 6);

    arp_send(p, sizeof(*p), nic);
    kfree(p);
}

void arp_handle(arp_packet_t* packet, uint32_t length, net_nic_interfaces_t* nic) {
    (void)length;
    if (!packet || !nic) return;

    uint16_t opcode = BSWAP16(packet->opcode);

    // Learn sender.
    arp_put_entry(packet->source_protocol_addr, packet->source_mac);

    if (opcode == 1) {
        // ARP request: if it's asking for our IP, reply.
        if (memcmp(packet->destination_protocol_addr, nic->ip_address, 4) != 0) {
            return;
        }

        arp_packet_t reply = *packet;
        memcpy(reply.destination_mac, packet->source_mac, 6);
        memcpy(reply.destination_protocol_addr, packet->source_protocol_addr, 4);

        memcpy(reply.source_mac, nic->get_mac_addr ? nic->get_mac_addr() : broadcast_mac, 6);
        memcpy(reply.source_protocol_addr, nic->ip_address, 4);

        reply.opcode = BSWAP16(2); // reply

        if (nic->send_packet) {
            nic->send_packet(reply.destination_mac, &reply, sizeof(reply), REQ_TYPE_ARP);
        }
    }
}
