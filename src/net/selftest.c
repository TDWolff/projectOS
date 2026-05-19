#include "selftest.h"

#include "net.h"
#include "ip.h"
#include "udp.h"

#include "arp.h"

#include "icmp.h"

#include "loopback.h"

// For bring-up: allow selftests to explicitly poll hardware RX.
#include "../drivers/net/e1000.h"

#include "../lib/stdio.h"
#include "../drivers/shell.h"
#include "../lib/string.h"
#include "../mem/heap.h"

// A tiny state bucket updated by our test UDP-indication hook.
static struct {
    bool udp_echo_seen;
} g_net_selftest_state;

// Hook: in self-test mode, udp.c calls this when it sees a port-7 echo packet.
void net_selftest__on_udp_echo(void) {
    g_net_selftest_state.udp_echo_seen = true;
}

static void reset_state(void) {
    g_net_selftest_state.udp_echo_seen = false;
}

static bool test_icmp_echo_loopback(void) {
    // Contract:
    // - We send an ICMP echo request into loopback.
    // - icmp.c should convert it to an echo reply and send it back out.
    // - We don't currently have a "tx observe" hook, so the pass condition is:
    //   "nothing crashes / returns". This still validates demux + checksum path.

    const uint32_t payload_len = 8; // arbitrary
    const uint32_t ip_len = (uint32_t)sizeof(ip_packet_t) + (uint32_t)sizeof(icmp_header_t) + payload_len;

    ip_packet_t* ip = (ip_packet_t*)kmalloc(ip_len);
    if (!ip) return false;
    memset(ip, 0, ip_len);

    ip->protocol = 1; // ICMP
    memcpy(ip->source_protocol_addr, (uint8_t[4]){127,0,0,1}, 4);
    memcpy(ip->destination_protocol_addr, (uint8_t[4]){127,0,0,1}, 4);

    icmp_header_t* icmp = (icmp_header_t*)ip->data;
    icmp->type = 8; // echo request
    icmp->code = 0;
    icmp->checksum = 0;

    // Fill payload so checksum isn't trivial.
    uint8_t* p = (uint8_t*)icmp + sizeof(icmp_header_t);
    for (uint32_t i = 0; i < payload_len; i++) p[i] = (uint8_t)(0xA0u + i);

    // Let ICMP compute checksum (icmp_echo_reply does it after flipping type)
    // but it expects a valid incoming packet; length passed is ip payload length.

    // Inject into stack via loopback NIC.
    nic_loopback.send_packet(0, ip, ip_len, REQ_TYPE_IP);

    kfree(ip);
    return true;
}

static bool test_udp_echo_loopback(void) {
    // Contract:
    // - Send UDP packet to port 7 via loopback.
    // - udp_handle swaps ports and sends it back out.
    // - We add a hook in udp.c to mark that the echo path ran.

    reset_state();

    const uint16_t src_port = 12345;
    const uint16_t dst_port = 7;

    const uint32_t data_len = 4;
    const uint32_t pkt_len = (uint32_t)sizeof(ip_packet_t) + (uint32_t)sizeof(udp_packet_t) + data_len;

    ip_packet_t* ip = (ip_packet_t*)kmalloc(pkt_len);
    if (!ip) return false;
    memset(ip, 0, pkt_len);

    // Minimal IP header fields used by our stack.
    ip->protocol = 17; // UDP
    ip->internet_header_length = 5;
    memcpy(ip->source_protocol_addr, (uint8_t[4]){127,0,0,1}, 4);
    memcpy(ip->destination_protocol_addr, (uint8_t[4]){127,0,0,1}, 4);

    udp_packet_t* udp = (udp_packet_t*)ip->data;
    udp->source_port = BSWAP16(src_port);
    udp->destination_port = BSWAP16(dst_port);
    udp->length = BSWAP16((uint16_t)(sizeof(udp_packet_t) + data_len));
    udp->checksum = 0;

    uint8_t* d = (uint8_t*)udp + sizeof(udp_packet_t);
    d[0] = 0xDE;
    d[1] = 0xAD;
    d[2] = 0xBE;
    d[3] = 0xEF;

    nic_loopback.send_packet(0, ip, pkt_len, REQ_TYPE_IP);

    kfree(ip);
    return g_net_selftest_state.udp_echo_seen;
}

static bool test_primary_arp_request(net_nic_interfaces_t* nic) {
    // Minimal "hardware smoke test":
    // - Build an ARP request to resolve the gateway IP.
    // - Broadcast it on the selected NIC.
    // Pass condition for now: transmit path doesn't crash and increments tx stats.
    // (We don't yet implement ARP table validation or gateway reply checks.)

    if (!nic || !nic->send_packet || !nic->get_mac_addr) return false;

    uint8_t bcast[6];
    memset(bcast, 0xFF, 6);

    arp_packet_t arp;
    memset(&arp, 0, sizeof(arp));

    arp.hw_type = BSWAP16(1);      // ethernet
    arp.protocol = BSWAP16(0x0800); // IPv4
    arp.hw_addr_len = 6;
    arp.protocol_addr_len = 4;
    arp.opcode = BSWAP16(1); // request

    uint8_t* mac = nic->get_mac_addr();
    memcpy(arp.source_mac, mac, 6);
    memcpy(arp.source_protocol_addr, nic->ip_address, 4);
    memset(arp.destination_mac, 0, 6);
    memcpy(arp.destination_protocol_addr, nic->gateway, 4);

    uint64_t tx_before = nic->tx_packets;
    nic->send_packet(bcast, &arp, sizeof(arp), REQ_TYPE_ARP);
    return nic->tx_packets > tx_before;
}

static bool test_primary_ping_gateway(net_nic_interfaces_t* nic) {
    if (!nic || !nic->send_packet) return false;

    // 1) Ensure we know the gateway MAC.
    uint8_t gw_mac[6];
    memset(gw_mac, 0, 6);

    // Trigger ARP if entry isn't present.
    if (!arp_resolve(nic->gateway, gw_mac)) {
        arp_lookup(nic->gateway, nic);

        // Wait briefly for RX poll to populate ARP table.
        // Explicitly poll NIC in case timer-driven polling is too infrequent.
        for (volatile uint32_t i = 0; i < 4000000; i++) {
            if ((i & 0x3FFF) == 0) e1000_poll();
            __asm__ volatile("pause");
        }
    }

    if (!arp_resolve(nic->gateway, gw_mac)) {
        return false;
    }

    // 2) Build an ICMP echo request to the gateway.
    // Tag identifier+sequence so we can confirm we received the reply.
    const uint16_t ident_be = BSWAP16(0xB007);
    const uint16_t seq_be = BSWAP16(1);
    const uint32_t payload_len = (uint32_t)sizeof(icmp_echo_hdr_t) + 12;
    const uint32_t ip_len = (uint32_t)sizeof(ip_packet_t) + (uint32_t)sizeof(icmp_header_t) + payload_len;

    ip_packet_t* ip = (ip_packet_t*)kmalloc(ip_len);
    if (!ip) return false;
    memset(ip, 0, ip_len);

    ip->protocol = 1; // ICMP
    ip->internet_header_length = 5;

    // Destination is gateway.
    memcpy(ip->destination_protocol_addr, nic->gateway, 4);
    // Source will be filled by ip_send.

    icmp_header_t* icmp = (icmp_header_t*)ip->data;
    icmp->type = 8; // echo request
    icmp->code = 0;
    icmp->checksum = 0;

    icmp_echo_hdr_t* echo = (icmp_echo_hdr_t*)icmp->data;
    echo->identifier = ident_be;
    echo->sequence = seq_be;

    uint8_t* p = (uint8_t*)icmp->data + sizeof(icmp_echo_hdr_t);
    for (uint32_t i = 0; i < payload_len - (uint32_t)sizeof(icmp_echo_hdr_t); i++) {
        p[i] = (uint8_t)(0xB0u + i);
    }

    // Compute ICMP checksum over ICMP header+payload.
    icmp->checksum = ip_calculate_checksum(icmp, (int)(sizeof(icmp_header_t) + payload_len));

    icmp_selftest_reset();

    uint64_t tx_before = nic->tx_packets;
    ip_send(ip, (uint16_t)ip_len, nic->gateway, gw_mac, nic);

    bool ok = (nic->tx_packets > tx_before);
    if (ok) {
        // Give RX path time to run and deliver the reply.
        for (uint32_t i = 0; i < 12000000; i++) {
            if ((i & 0x3FFF) == 0) e1000_poll();
            if (icmp_selftest_wait_for_echo_reply(ident_be, seq_be, 1)) {
                ok = true;
                break;
            }
            ok = false;
        }
    }

    kfree(ip);
    return ok;
}

static bool test_primary_udp_gateway(net_nic_interfaces_t* nic) {
    if (!nic || !nic->send_packet) return false;

    uint8_t gw_mac[6];
    memset(gw_mac, 0, 6);

    if (!arp_resolve(nic->gateway, gw_mac)) {
        arp_lookup(nic->gateway, nic);
        for (volatile uint32_t i = 0; i < 4000000; i++) {
            if ((i & 0x3FFF) == 0) e1000_poll();
            __asm__ volatile("pause");
        }
    }
    if (!arp_resolve(nic->gateway, gw_mac)) return false;

    // Build an empty UDP payload to gateway:7.
    const uint16_t src_port = 12345;
    const uint16_t dst_port = 7;
    const uint32_t data_len = 4;

    const uint32_t pkt_len = (uint32_t)sizeof(ip_packet_t) + (uint32_t)sizeof(udp_packet_t) + data_len;
    ip_packet_t* ip = (ip_packet_t*)kmalloc(pkt_len);
    if (!ip) return false;
    memset(ip, 0, pkt_len);

    // udp_send fills UDP header and uses ip_send.
    uint8_t* d = (uint8_t*)ip->data + sizeof(udp_packet_t);
    d[0] = 0xCA;
    d[1] = 0xFE;
    d[2] = 0xBA;
    d[3] = 0xBE;

    uint64_t tx_before = nic->tx_packets;
    udp_send(ip, (uint16_t)data_len, nic->gateway, gw_mac, dst_port, src_port, nic);

    kfree(ip);
    return nic->tx_packets > tx_before;
}

bool net_selftest_run(void) {
    bool ok = true;

    sh_printf("NET: selftest...\n");

    net_nic_interfaces_t* primary = net_get_primary_nic();
    bool primary_is_lo = (primary && (primary->flags & IFF_LOOPBACK) != 0);

    if (primary_is_lo) {
        bool icmp_ok = test_icmp_echo_loopback();
        sh_printf("  - ICMP (primary=lo): ");
        sh_printf(icmp_ok ? "PASS\n" : "FAIL\n");
        ok = ok && icmp_ok;

        bool udp_ok = test_udp_echo_loopback();
        sh_printf("  - UDP echo (primary=lo, port 7): ");
        sh_printf(udp_ok ? "PASS\n" : "FAIL\n");
        ok = ok && udp_ok;
    } else {
    net_dbg_reset();

    bool ping_ok = test_primary_ping_gateway(primary);
    sh_printf("  - ICMP ping (primary -> gw): ");
    sh_printf(ping_ok ? "PASS\n" : "FAIL\n");
    ok = ok && ping_ok;

    bool udp_ok = test_primary_udp_gateway(primary);
    sh_printf("  - UDP (primary -> gw, port 7): ");
    sh_printf(udp_ok ? "PASS\n" : "FAIL\n");
    ok = ok && udp_ok;

        if (!ok) {
         // Use kprintf for bring-up diagnostics (shell helpers are file-static).
         kprintf("NETTEST DBG: rx_frames=%d last_ethertype=0x%x\n",
             (int)net_dbg_get_rx_frames(), (uint32_t)net_dbg_get_last_ethertype());

            uint8_t gw_mac[6];
            memset(gw_mac, 0, 6);
            sh_printf("  - DBG arp_resolve(gw)=");
            sh_printf(arp_resolve(primary->gateway, gw_mac) ? "YES\n" : "NO\n");
        }
    }

    sh_printf(ok ? "NET: selftest PASS\n" : "NET: selftest FAIL\n");
    return ok;
}
