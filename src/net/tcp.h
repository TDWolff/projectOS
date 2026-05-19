#ifndef TCP_H
#define TCP_H

#include "../include/types.h"
#include "net.h"
#include "ip.h"

// TCP flag bits
#define TCP_FIN (1u << 0)
#define TCP_SYN (1u << 1)
#define TCP_RST (1u << 2)
#define TCP_PSH (1u << 3)
#define TCP_ACK (1u << 4)
#define TCP_URG (1u << 5)

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack_num;
    uint8_t  data_off;   // top 4 bits = header length in 32-bit words
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
    uint8_t  data[];
} tcp_hdr_t;

typedef enum {
    TCP_CLOSED = 0,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_TIME_WAIT,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK,
} tcp_state_t;

#define TCP_RECV_BUF 8192

typedef struct {
    tcp_state_t   state;
    uint8_t       remote_ip[4];
    uint16_t      remote_port;
    uint16_t      local_port;
    uint32_t      seq;          // next seq we will send
    uint32_t      ack;          // next seq we expect from remote
    uint32_t      remote_win;

    uint8_t       rx_buf[TCP_RECV_BUF];
    uint32_t      rx_head;      // write index
    uint32_t      rx_tail;      // read index
    uint32_t      rx_len;       // bytes available

    uint8_t             remote_mac[6];
    net_nic_interfaces_t* nic;
} tcp_conn_t;

// Connect to remote_ip:remote_port. Blocks until ESTABLISHED or timeout.
// Returns NULL on failure.
tcp_conn_t* tcp_connect(uint8_t remote_ip[4], uint16_t remote_port,
                        net_nic_interfaces_t* nic);

// Send data on an established connection. Returns bytes queued.
uint32_t tcp_send(tcp_conn_t* conn, const uint8_t* data, uint32_t len);

// Copy up to max_len bytes of received data into buf. Returns bytes copied.
uint32_t tcp_recv(tcp_conn_t* conn, uint8_t* buf, uint32_t max_len);

// Send FIN and begin graceful close.
void tcp_close(tcp_conn_t* conn);

// Release the connection slot back to the pool (call after tcp_close + done).
void tcp_free(tcp_conn_t* conn);

// True while in ESTABLISHED state.
bool tcp_is_connected(tcp_conn_t* conn);

// True once CLOSED and rx buffer is empty (nothing left to read).
bool tcp_is_done(tcp_conn_t* conn);

// Called from ip_handle for protocol-6 packets.
void tcp_handle(ip_packet_t* packet, uint32_t length,
                uint8_t src_mac[6], net_nic_interfaces_t* nic);

#endif
