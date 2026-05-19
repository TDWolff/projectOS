#include "dns.h"
#include "ip.h"
#include "udp.h"
#include "arp.h"
#include "../lib/string.h"
#include "../mem/heap.h"
#include "../drivers/net/e1000.h"
#include "../drivers/timer.h"

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

#define DNS_FLAG_QR  (1u << 15)
#define DNS_FLAG_RD  (1u << 8)
#define DNS_RCODE_OK 0
#define DNS_TYPE_A   1
#define DNS_CLASS_IN 1
#define DNS_TXN_ID   0x1D53u

static struct {
    bool    done;
    bool    success;
    uint8_t ip[4];
} g_dns_result;

static uint32_t g_dbg_rx_count       = 0;
static uint32_t g_dbg_id_mismatch    = 0;

uint32_t dns_dbg_rx_count(void)          { return g_dbg_rx_count; }
uint32_t dns_dbg_id_mismatch_count(void) { return g_dbg_id_mismatch; }

// "google.com" -> 0x06 "google" 0x03 "com" 0x00
// Returns bytes written, 0 on error.
static uint32_t dns_encode_name(const char* hostname, uint8_t* buf, uint32_t buf_size) {
    uint32_t total = 0;
    const char* p = hostname;
    while (*p) {
        const char* dot = p;
        while (*dot && *dot != '.') dot++;
        uint32_t label_len = (uint32_t)(dot - p);
        if (label_len == 0) { p = dot + 1; continue; } // skip empty (trailing dot)
        if (label_len > 63 || total + 1 + label_len + 1 > buf_size) return 0;
        buf[total++] = (uint8_t)label_len;
        for (uint32_t i = 0; i < label_len; i++) buf[total++] = (uint8_t)p[i];
        p = dot;
        if (*p == '.') p++;
    }
    if (total + 1 > buf_size) return 0;
    buf[total++] = 0; // root label
    return total;
}

// Returns offset after the name. Handles pointer compression (0xC0xx).
static uint32_t dns_skip_name(const uint8_t* buf, uint32_t len, uint32_t offset) {
    uint32_t iters = 0;
    while (offset < len && iters++ < 128) {
        uint8_t c = buf[offset];
        if (c == 0) return offset + 1;
        if ((c & 0xC0) == 0xC0) return offset + 2;
        offset += 1 + (uint32_t)c;
    }
    return len;
}

void dns_handle_response(uint8_t* data, uint32_t len) {
    g_dbg_rx_count++;
    if (!data || len < sizeof(dns_header_t)) return;

    dns_header_t* hdr = (dns_header_t*)data;
    if (hdr->id != BSWAP16(DNS_TXN_ID)) {
        g_dbg_id_mismatch++;
        return;
    }

    uint16_t flags = BSWAP16(hdr->flags);
    if (!(flags & DNS_FLAG_QR)) return;
    if ((flags & 0xF) != DNS_RCODE_OK) { g_dns_result.done = true; return; }

    uint16_t ancount = BSWAP16(hdr->ancount);
    uint16_t qdcount = BSWAP16(hdr->qdcount);
    if (ancount == 0) { g_dns_result.done = true; return; }

    // Skip question section
    uint32_t offset = (uint32_t)sizeof(dns_header_t);
    for (uint16_t q = 0; q < qdcount && offset < len; q++) {
        offset = dns_skip_name(data, len, offset);
        offset += 4; // QTYPE + QCLASS
    }

    // Scan answer RRs for first A record
    for (uint16_t a = 0; a < ancount && offset < len; a++) {
        offset = dns_skip_name(data, len, offset);
        if (offset + 10 > len) break;

        uint16_t rtype_be, rclass_be, rdlen_be;
        memcpy(&rtype_be,  data + offset, 2); offset += 2;
        memcpy(&rclass_be, data + offset, 2); offset += 2;
        offset += 4; // TTL
        memcpy(&rdlen_be, data + offset, 2);  offset += 2;

        uint16_t rtype  = BSWAP16(rtype_be);
        uint16_t rclass = BSWAP16(rclass_be);
        uint16_t rdlen  = BSWAP16(rdlen_be);

        if (rtype == DNS_TYPE_A && rclass == DNS_CLASS_IN && rdlen == 4 && offset + 4 <= len) {
            memcpy(g_dns_result.ip, data + offset, 4);
            g_dns_result.done    = true;
            g_dns_result.success = true;
            return;
        }
        offset += rdlen;
    }

    g_dns_result.done = true;
}

bool dns_resolve(const char* hostname, uint8_t out_ip[4],
                 net_nic_interfaces_t* nic, uint8_t dns_server[4]) {
    if (!hostname || !out_ip || !nic) return false;

    // Prefer: explicit arg → DHCP-provided DNS → 8.8.8.8 last resort
    uint8_t fallback_dns[4] = {8, 8, 8, 8};
    uint8_t* dns_ip;
    if (dns_server) {
        dns_ip = dns_server;
    } else if (nic->dns_server[0] != 0) {
        dns_ip = nic->dns_server;
    } else {
        dns_ip = fallback_dns;
    }

    // Next-hop: ARP the DNS server directly if on-subnet, else ARP the gateway.
    bool on_subnet = true;
    for (int i = 0; i < 4; i++) {
        if ((dns_ip[i] & nic->subnet[i]) != (nic->ip_address[i] & nic->subnet[i])) {
            on_subnet = false;
            break;
        }
    }
    uint8_t* nexthop = on_subnet ? dns_ip : nic->gateway;

    uint8_t dest_mac[6] = {0};
    if (!arp_resolve(nexthop, dest_mac)) {
        arp_lookup(nexthop, nic);
        __asm__ volatile("sti");
        uint64_t deadline = get_ticks() + 2000;
        while (get_ticks() < deadline) {
            e1000_poll();
            if (arp_resolve(nexthop, dest_mac)) break;
            __asm__ volatile("pause");
        }
        __asm__ volatile("cli");
    }
    if (!arp_resolve(nexthop, dest_mac)) return false;

    // Encode hostname into DNS wire format
    uint8_t name_buf[256];
    uint32_t name_len = dns_encode_name(hostname, name_buf, sizeof(name_buf));
    if (name_len == 0) return false;

    // Allocate: IP header + UDP header + DNS header + question
    uint32_t payload_len = (uint32_t)sizeof(dns_header_t) + name_len + 4; // +4: QTYPE+QCLASS
    uint32_t buf_size    = (uint32_t)sizeof(ip_packet_t) + (uint32_t)sizeof(udp_packet_t) + payload_len;

    uint8_t* buf = (uint8_t*)kmalloc(buf_size);
    if (!buf) return false;
    memset(buf, 0, buf_size);

    // DNS payload lives after IP + UDP headers
    uint8_t* dns_payload = buf + sizeof(ip_packet_t) + sizeof(udp_packet_t);

    dns_header_t* hdr = (dns_header_t*)dns_payload;
    hdr->id      = BSWAP16(DNS_TXN_ID);
    hdr->flags   = BSWAP16((uint16_t)DNS_FLAG_RD);
    hdr->qdcount = BSWAP16(1);

    uint8_t* question = dns_payload + sizeof(dns_header_t);
    memcpy(question, name_buf, name_len);
    question[name_len + 0] = 0;
    question[name_len + 1] = DNS_TYPE_A;
    question[name_len + 2] = 0;
    question[name_len + 3] = DNS_CLASS_IN;

    g_dns_result.done    = false;
    g_dns_result.success = false;

    udp_send((ip_packet_t*)buf, (uint16_t)payload_len, dns_ip, dest_mac,
             DNS_SERVER_PORT, DNS_CLIENT_PORT, nic);
    kfree(buf);

    // Wait up to 5 seconds for the response
    __asm__ volatile("sti");
    uint64_t deadline = get_ticks() + 5000;
    while (get_ticks() < deadline) {
        e1000_poll();
        if (g_dns_result.done) break;
        __asm__ volatile("pause");
    }
    __asm__ volatile("cli");

    if (g_dns_result.done && g_dns_result.success) {
        memcpy(out_ip, g_dns_result.ip, 4);
        return true;
    }
    return false;
}
