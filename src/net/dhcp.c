#include "dhcp.h"
#include "ip.h"
#include "net.h"

#include "../lib/string.h"
#include "../mem/heap.h"
#include "../lib/stdio.h"
#include "../drivers/net/e1000.h"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

typedef enum {
    DHCP_STATE_INIT,
    DHCP_STATE_SELECTING,
    DHCP_STATE_REQUESTING,
    DHCP_STATE_BOUND,
} dhcp_state_t;

static dhcp_state_t g_state      = DHCP_STATE_INIT;
static uint32_t     g_xid        = 0xC0FFEE01u;
static bool         g_got_offer  = false;
static bool         g_got_ack    = false;

static uint8_t g_offered_ip[4] = {0};
static uint8_t g_server_ip[4]  = {0};
static uint8_t g_subnet[4]     = {0};
static uint8_t g_gateway[4]    = {0};
static uint8_t g_dns[4]        = {0};

bool dhcp_is_bound(void) { return g_state == DHCP_STATE_BOUND; }

// ---------------------------------------------------------------------------
// Option parsing helpers
// ---------------------------------------------------------------------------

static uint8_t parse_opt_u8(uint8_t* opts, uint32_t opts_len, uint8_t code) {
    uint32_t i = 0;
    while (i < opts_len) {
        uint8_t t = opts[i++];
        if (t == DHCP_OPT_END) break;
        if (t == 0) continue; // pad
        if (i >= opts_len) break;
        uint8_t l = opts[i++];
        if (t == code && l >= 1) return opts[i];
        i += l;
    }
    return 0;
}

static void parse_opt_ip(uint8_t* opts, uint32_t opts_len, uint8_t code, uint8_t out[4]) {
    uint32_t i = 0;
    while (i < opts_len) {
        uint8_t t = opts[i++];
        if (t == DHCP_OPT_END) break;
        if (t == 0) continue;
        if (i >= opts_len) break;
        uint8_t l = opts[i++];
        if (t == code && l >= 4) { memcpy(out, &opts[i], 4); return; }
        i += l;
    }
}

// ---------------------------------------------------------------------------
// Packet builder — sends IP/UDP/DHCP manually so we can use src=0.0.0.0
// before we have a real IP.
// ---------------------------------------------------------------------------

static void dhcp_send(uint8_t msg_type, net_nic_interfaces_t* nic) {
    // Determine options size
    uint32_t opts_len;
    if (msg_type == DHCP_MSG_DISCOVER) {
        opts_len = 3 + 4; // type(3) + param list(4) + end(1) = ... let's just use fixed 12
        opts_len = 12;
    } else {
        opts_len = 16; // type(3) + req ip(6) + server id(6) + end(1)
    }

    uint32_t dhcp_len  = (uint32_t)sizeof(dhcp_packet_t) + opts_len;
    uint32_t udp_len   = (uint32_t)sizeof(udp_packet_t)  + dhcp_len;
    uint32_t ip_len    = (uint32_t)sizeof(ip_packet_t)   + udp_len;

    uint8_t* buf = (uint8_t*)kmalloc(ip_len);
    if (!buf) return;
    memset(buf, 0, ip_len);

    // --- IP header (src=0.0.0.0, dst=255.255.255.255) ---
    ip_packet_t* ip = (ip_packet_t*)buf;
    ip->internet_header_length = 5;
    ip->version                = 4;
    ip->type_of_service        = 0;
    ip->length                 = BSWAP16((uint16_t)ip_len);
    ip->id                     = 0;
    ip->fragment_offset        = BSWAP16(0x4000); // DF
    ip->time_to_live           = 64;
    ip->protocol               = 17; // UDP
    memset(ip->source_protocol_addr,      0x00, 4);
    memset(ip->destination_protocol_addr, 0xFF, 4);
    ip->checksum = 0;
    ip->checksum = ip_calculate_checksum(ip, (int)sizeof(ip_packet_t));

    // --- UDP header ---
    udp_packet_t* udp = (udp_packet_t*)ip->data;
    udp->source_port      = BSWAP16(68);
    udp->destination_port = BSWAP16(67);
    udp->length           = BSWAP16((uint16_t)udp_len);
    udp->checksum         = 0; // optional, skip

    // --- DHCP payload ---
    dhcp_packet_t* dhcp = (dhcp_packet_t*)udp->data;
    dhcp->op    = 1; // BOOTREQUEST
    dhcp->htype = 1; // Ethernet
    dhcp->hlen  = 6;
    dhcp->hops  = 0;
    dhcp->xid   = g_xid;
    dhcp->secs  = 0;
    dhcp->flags = BSWAP16(0x8000); // broadcast flag

    uint8_t* mac = nic->get_mac_addr ? nic->get_mac_addr() : 0;
    if (mac) memcpy(dhcp->chaddr, mac, 6);

    dhcp->magic = BSWAP32(DHCP_MAGIC_COOKIE);

    // --- Options ---
    uint8_t* opt = dhcp->options;
    uint32_t o   = 0;

    // Option 53: DHCP message type
    opt[o++] = DHCP_OPT_MSG_TYPE;
    opt[o++] = 1;
    opt[o++] = msg_type;

    if (msg_type == DHCP_MSG_DISCOVER) {
        // Option 55: parameter request list
        opt[o++] = DHCP_OPT_PARAM_LIST;
        opt[o++] = 2;
        opt[o++] = DHCP_OPT_SUBNET_MASK;
        opt[o++] = DHCP_OPT_ROUTER;
    } else {
        // Option 50: requested IP
        opt[o++] = DHCP_OPT_REQ_IP;
        opt[o++] = 4;
        memcpy(&opt[o], g_offered_ip, 4); o += 4;

        // Option 54: server identifier
        opt[o++] = DHCP_OPT_SERVER_ID;
        opt[o++] = 4;
        memcpy(&opt[o], g_server_ip, 4); o += 4;
    }

    opt[o++] = DHCP_OPT_END;

    // Send as broadcast Ethernet frame
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    nic->send_packet(bcast, buf, ip_len, REQ_TYPE_IP);

    kfree(buf);
}

// ---------------------------------------------------------------------------
// Inbound handler (called from udp_handle for port 68)
// ---------------------------------------------------------------------------

void dhcp_handle(dhcp_packet_t* pkt, uint32_t length, net_nic_interfaces_t* nic) {
    (void)nic;
    if (!pkt || length < sizeof(dhcp_packet_t)) return;
    if (BSWAP32(pkt->magic) != DHCP_MAGIC_COOKIE) return;
    if (pkt->op != 2) return; // only process BOOTREPLY
    if (pkt->xid != g_xid) return; // not our transaction

    uint32_t opts_len = length - (uint32_t)sizeof(dhcp_packet_t);
    uint8_t  msg_type = parse_opt_u8(pkt->options, opts_len, DHCP_OPT_MSG_TYPE);

    if (msg_type == DHCP_MSG_OFFER && g_state == DHCP_STATE_SELECTING) {
        memcpy(g_offered_ip, pkt->yiaddr, 4);
        parse_opt_ip(pkt->options, opts_len, DHCP_OPT_SERVER_ID, g_server_ip);
        // Fallback: use siaddr if option 54 missing
        if (g_server_ip[0] == 0) memcpy(g_server_ip, pkt->siaddr, 4);
        g_got_offer = true;
    }

    if (msg_type == DHCP_MSG_ACK && g_state == DHCP_STATE_REQUESTING) {
        memcpy(g_offered_ip, pkt->yiaddr, 4);
        parse_opt_ip(pkt->options, opts_len, DHCP_OPT_SUBNET_MASK, g_subnet);
        parse_opt_ip(pkt->options, opts_len, DHCP_OPT_ROUTER,      g_gateway);
        parse_opt_ip(pkt->options, opts_len, DHCP_OPT_DNS_SERVER,  g_dns);
        g_got_ack = true;
    }
}

// ---------------------------------------------------------------------------
// Public: run the full DHCP handshake
// ---------------------------------------------------------------------------

bool dhcp_request_lease(net_nic_interfaces_t* nic) {
    if (!nic || !nic->send_packet) return false;

    g_state     = DHCP_STATE_SELECTING;
    g_got_offer = false;
    g_got_ack   = false;

    dhcp_send(DHCP_MSG_DISCOVER, nic);

    // Wait for Offer (poll NIC)
    for (volatile uint32_t i = 0; i < 8000000u; i++) {
        if ((i & 0x3FFFu) == 0) e1000_poll();
        if (g_got_offer) break;
        __asm__ volatile("pause");
    }

    if (!g_got_offer) {
        g_state = DHCP_STATE_INIT;
        return false;
    }

    g_state = DHCP_STATE_REQUESTING;
    dhcp_send(DHCP_MSG_REQUEST, nic);

    // Wait for ACK
    for (volatile uint32_t i = 0; i < 8000000u; i++) {
        if ((i & 0x3FFFu) == 0) e1000_poll();
        if (g_got_ack) break;
        __asm__ volatile("pause");
    }

    if (!g_got_ack) {
        g_state = DHCP_STATE_INIT;
        return false;
    }

    // Apply the lease to the NIC
    memcpy(nic->ip_address, g_offered_ip, 4);
    if (g_subnet[0]  != 0) memcpy(nic->subnet,     g_subnet, 4);
    if (g_gateway[0] != 0) memcpy(nic->gateway,    g_gateway, 4);
    if (g_dns[0]     != 0) memcpy(nic->dns_server, g_dns,     4);

    g_state = DHCP_STATE_BOUND;
    return true;
}
