#include "tcp.h"
#include "arp.h"
#include "ip.h"
#include "../lib/string.h"
#include "../mem/heap.h"
#include "../drivers/net/e1000.h"
#include "../drivers/timer.h"

// ---------------------------------------------------------------------------
// Connection pool
// ---------------------------------------------------------------------------

#define TCP_MAX_CONNS 8
static tcp_conn_t g_conns[TCP_MAX_CONNS];
static uint16_t   g_next_port = 49152;

static tcp_conn_t* tcp_alloc(void) {
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (g_conns[i].state == TCP_CLOSED) {
            memset(&g_conns[i], 0, sizeof(tcp_conn_t));
            return &g_conns[i];
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// TCP checksum (RFC 793 pseudo-header method)
// ---------------------------------------------------------------------------

typedef struct __attribute__((packed)) {
    uint8_t  src_ip[4];
    uint8_t  dst_ip[4];
    uint8_t  zero;
    uint8_t  proto;
    uint16_t tcp_len;
} tcp_pseudo_t;

static uint16_t tcp_checksum(uint8_t* src_ip, uint8_t* dst_ip,
                              void* seg, uint16_t seg_len) {
    uint32_t total = (uint32_t)sizeof(tcp_pseudo_t) + seg_len;
    uint8_t* buf   = (uint8_t*)kmalloc(total);
    if (!buf) return 0;

    tcp_pseudo_t* ph = (tcp_pseudo_t*)buf;
    memcpy(ph->src_ip, src_ip, 4);
    memcpy(ph->dst_ip, dst_ip, 4);
    ph->zero    = 0;
    ph->proto   = 6;
    ph->tcp_len = BSWAP16(seg_len);
    memcpy(buf + sizeof(tcp_pseudo_t), seg, seg_len);

    uint16_t cs = ip_calculate_checksum(buf, (int)total);
    kfree(buf);
    return cs;
}

// ---------------------------------------------------------------------------
// Internal segment emit
// ---------------------------------------------------------------------------

static void tcp_emit(tcp_conn_t* conn, uint8_t flags,
                     const uint8_t* data, uint32_t data_len) {
    uint32_t hdr_len = sizeof(tcp_hdr_t); // 20 bytes, no options
    uint32_t tcp_len = hdr_len + data_len;
    uint32_t ip_len  = sizeof(ip_packet_t) + tcp_len;

    ip_packet_t* ip = (ip_packet_t*)kmalloc(ip_len);
    if (!ip) return;
    memset(ip, 0, ip_len);

    ip->protocol               = 6;
    ip->internet_header_length = 5;
    ip->time_to_live           = 64;

    tcp_hdr_t* tcp = (tcp_hdr_t*)ip->data;
    tcp->src_port = BSWAP16(conn->local_port);
    tcp->dst_port = BSWAP16(conn->remote_port);
    tcp->seq      = BSWAP32(conn->seq);
    tcp->ack_num  = (flags & TCP_ACK) ? BSWAP32(conn->ack) : 0;
    tcp->data_off = (uint8_t)((hdr_len / 4) << 4); // 0x50 for 20-byte header
    tcp->flags    = flags;
    tcp->window   = BSWAP16(TCP_RECV_BUF);
    tcp->checksum = 0;
    tcp->urgent   = 0;

    if (data && data_len > 0)
        memcpy(tcp->data, data, data_len);

    tcp->checksum = tcp_checksum(conn->nic->ip_address, conn->remote_ip,
                                  tcp, (uint16_t)tcp_len);

    ip_send(ip, (uint16_t)ip_len, conn->remote_ip, conn->remote_mac, conn->nic);
    kfree(ip);
}

// ---------------------------------------------------------------------------
// Push incoming data into the connection's receive ring buffer
// ---------------------------------------------------------------------------

static void tcp_rx_push(tcp_conn_t* conn, const uint8_t* data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (conn->rx_len < TCP_RECV_BUF) {
            conn->rx_buf[conn->rx_head] = data[i];
            conn->rx_head = (conn->rx_head + 1) % TCP_RECV_BUF;
            conn->rx_len++;
        }
        // silently drop if buffer full
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

tcp_conn_t* tcp_connect(uint8_t remote_ip[4], uint16_t remote_port,
                        net_nic_interfaces_t* nic) {
    if (!nic) return 0;

    // Next-hop: ARP remote directly if on-subnet, else ARP gateway
    bool on_subnet = true;
    for (int i = 0; i < 4; i++) {
        if ((remote_ip[i] & nic->subnet[i]) != (nic->ip_address[i] & nic->subnet[i])) {
            on_subnet = false;
            break;
        }
    }
    uint8_t* nexthop = on_subnet ? remote_ip : nic->gateway;

    uint8_t mac[6] = {0};
    if (!arp_resolve(nexthop, mac)) {
        arp_lookup(nexthop, nic);
        __asm__ volatile("sti");
        uint64_t dl = get_ticks() + 2000;
        while (get_ticks() < dl) {
            e1000_poll();
            if (arp_resolve(nexthop, mac)) break;
            __asm__ volatile("pause");
        }
        __asm__ volatile("cli");
    }
    if (!arp_resolve(nexthop, mac)) return 0;

    tcp_conn_t* conn = tcp_alloc();
    if (!conn) return 0;

    memcpy(conn->remote_ip,  remote_ip, 4);
    memcpy(conn->remote_mac, mac, 6);
    conn->remote_port = remote_port;
    conn->local_port  = g_next_port++;
    if (g_next_port < 49152) g_next_port = 49152; // wrap-around guard
    conn->nic         = nic;
    conn->seq         = 0xC0DE0000u;  // ISN (arbitrary)
    conn->ack         = 0;
    conn->remote_win  = 65535;
    conn->state       = TCP_SYN_SENT;

    tcp_emit(conn, TCP_SYN, 0, 0);
    conn->seq++; // SYN consumes one sequence number

    // Wait up to 5 seconds for SYN-ACK
    __asm__ volatile("sti");
    uint64_t deadline = get_ticks() + 5000;
    while (get_ticks() < deadline) {
        e1000_poll();
        if (conn->state == TCP_ESTABLISHED || conn->state == TCP_CLOSED) break;
        __asm__ volatile("pause");
    }
    __asm__ volatile("cli");

    if (conn->state != TCP_ESTABLISHED) {
        memset(conn, 0, sizeof(tcp_conn_t)); // free the slot
        return 0;
    }
    return conn;
}

uint32_t tcp_send(tcp_conn_t* conn, const uint8_t* data, uint32_t len) {
    if (!conn || conn->state != TCP_ESTABLISHED || !data || len == 0) return 0;

    const uint32_t MSS = 1460;
    uint32_t sent = 0;
    while (sent < len) {
        uint32_t chunk = len - sent;
        if (chunk > MSS) chunk = MSS;
        tcp_emit(conn, TCP_PSH | TCP_ACK, data + sent, chunk);
        conn->seq += chunk;
        sent += chunk;
    }
    return sent;
}

uint32_t tcp_recv(tcp_conn_t* conn, uint8_t* buf, uint32_t max_len) {
    if (!conn || !buf || conn->rx_len == 0) return 0;

    uint32_t n = (conn->rx_len < max_len) ? conn->rx_len : max_len;
    for (uint32_t i = 0; i < n; i++) {
        buf[i] = conn->rx_buf[conn->rx_tail];
        conn->rx_tail = (conn->rx_tail + 1) % TCP_RECV_BUF;
    }
    conn->rx_len -= n;
    return n;
}

void tcp_close(tcp_conn_t* conn) {
    if (!conn) return;
    if (conn->state == TCP_ESTABLISHED) {
        tcp_emit(conn, TCP_FIN | TCP_ACK, 0, 0);
        conn->seq++;
        conn->state = TCP_FIN_WAIT_1;
    } else if (conn->state == TCP_CLOSE_WAIT) {
        tcp_emit(conn, TCP_FIN | TCP_ACK, 0, 0);
        conn->seq++;
        conn->state = TCP_LAST_ACK;
    }
}

void tcp_free(tcp_conn_t* conn) {
    if (conn) conn->state = TCP_CLOSED;
}

bool tcp_is_connected(tcp_conn_t* conn) {
    return conn && conn->state == TCP_ESTABLISHED;
}

bool tcp_is_done(tcp_conn_t* conn) {
    return !conn || (conn->state == TCP_CLOSED && conn->rx_len == 0);
}

// ---------------------------------------------------------------------------
// Inbound handler (called from ip_handle, protocol == 6)
// ---------------------------------------------------------------------------

void tcp_handle(ip_packet_t* packet, uint32_t length,
                uint8_t src_mac[6], net_nic_interfaces_t* nic) {
    (void)src_mac; (void)nic;
    if (!packet || length < sizeof(ip_packet_t) + sizeof(tcp_hdr_t)) return;

    tcp_hdr_t* tcp = (tcp_hdr_t*)packet->data;
    uint16_t dst_port = BSWAP16(tcp->dst_port);
    uint16_t src_port = BSWAP16(tcp->src_port);

    // Find matching connection by (local_port, remote_port, remote_ip)
    tcp_conn_t* conn = 0;
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        tcp_conn_t* c = &g_conns[i];
        if (c->state == TCP_CLOSED) continue;
        if (c->local_port  == dst_port &&
            c->remote_port == src_port &&
            memcmp(c->remote_ip, packet->source_protocol_addr, 4) == 0) {
            conn = c;
            break;
        }
    }
    if (!conn) return;

    // Parse TCP header length and data
    uint32_t tcp_hdr_len = (uint32_t)(tcp->data_off >> 4) * 4;
    if (tcp_hdr_len < sizeof(tcp_hdr_t)) return;

    uint32_t ip_total = (uint32_t)BSWAP16(packet->length);
    uint32_t ip_hdr   = (uint32_t)packet->internet_header_length * 4;
    uint32_t data_len = (ip_total > ip_hdr + tcp_hdr_len)
                       ? ip_total - ip_hdr - tcp_hdr_len : 0;
    uint8_t* data_ptr  = (uint8_t*)tcp + tcp_hdr_len;
    uint32_t rem_seq   = BSWAP32(tcp->seq);
    uint32_t rem_ack   = BSWAP32(tcp->ack_num);

    // RST: abort immediately
    if (tcp->flags & TCP_RST) {
        conn->state = TCP_CLOSED;
        return;
    }

    switch (conn->state) {

    case TCP_SYN_SENT:
        // Expect SYN+ACK from server
        if ((tcp->flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            conn->ack        = rem_seq + 1; // server's ISN + 1
            conn->seq        = rem_ack;     // update to server-acked value
            conn->remote_win = BSWAP16(tcp->window);
            tcp_emit(conn, TCP_ACK, 0, 0);
            conn->state = TCP_ESTABLISHED;
        }
        break;

    case TCP_ESTABLISHED:
        if (tcp->flags & TCP_ACK)
            conn->remote_win = BSWAP16(tcp->window);

        // Buffer any data
        if (data_len > 0) {
            tcp_rx_push(conn, data_ptr, data_len);
            conn->ack = rem_seq + data_len;
        }

        if (tcp->flags & TCP_FIN) {
            // Remote is closing; ACK the FIN, then immediately close our side
            conn->ack = rem_seq + data_len + 1;
            tcp_emit(conn, TCP_ACK, 0, 0);
            conn->state = TCP_CLOSE_WAIT;
            tcp_emit(conn, TCP_FIN | TCP_ACK, 0, 0);
            conn->seq++;
            conn->state = TCP_LAST_ACK;
        } else if (data_len > 0) {
            tcp_emit(conn, TCP_ACK, 0, 0);
        }
        break;

    case TCP_FIN_WAIT_1:
        // Data can still arrive after we sent FIN
        if (data_len > 0) {
            tcp_rx_push(conn, data_ptr, data_len);
            conn->ack = rem_seq + data_len;
            tcp_emit(conn, TCP_ACK, 0, 0);
        }
        if ((tcp->flags & (TCP_FIN | TCP_ACK)) == (TCP_FIN | TCP_ACK)) {
            // Server ACK'd our FIN and also sent its own FIN
            conn->ack = rem_seq + data_len + 1;
            tcp_emit(conn, TCP_ACK, 0, 0);
            conn->state = TCP_CLOSED;
        } else if (tcp->flags & TCP_ACK) {
            conn->state = TCP_FIN_WAIT_2;
        } else if (tcp->flags & TCP_FIN) {
            // Simultaneous close
            conn->ack = rem_seq + 1;
            tcp_emit(conn, TCP_ACK, 0, 0);
            conn->state = TCP_CLOSED;
        }
        break;

    case TCP_FIN_WAIT_2:
        if (data_len > 0) {
            tcp_rx_push(conn, data_ptr, data_len);
            conn->ack = rem_seq + data_len;
            tcp_emit(conn, TCP_ACK, 0, 0);
        }
        if (tcp->flags & TCP_FIN) {
            conn->ack = rem_seq + data_len + 1;
            tcp_emit(conn, TCP_ACK, 0, 0);
            conn->state = TCP_CLOSED;
        }
        break;

    case TCP_LAST_ACK:
        if (tcp->flags & TCP_ACK)
            conn->state = TCP_CLOSED;
        break;

    default:
        break;
    }
}
