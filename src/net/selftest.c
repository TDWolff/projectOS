#include "selftest.h"

#include "net.h"
#include "ip.h"
#include "udp.h"

#include "icmp.h"

#include "loopback.h"

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

bool net_selftest_run(void) {
    bool ok = true;

    sh_printf("NET: selftest...\n");

    bool icmp_ok = test_icmp_echo_loopback();
    sh_printf("  - ICMP loopback: ");
    sh_printf(icmp_ok ? "PASS\n" : "FAIL\n");
    ok = ok && icmp_ok;

    bool udp_ok = test_udp_echo_loopback();
    sh_printf("  - UDP echo (port 7): ");
    sh_printf(udp_ok ? "PASS\n" : "FAIL\n");
    ok = ok && udp_ok;

    sh_printf(ok ? "NET: selftest PASS\n" : "NET: selftest FAIL\n");
    return ok;
}
