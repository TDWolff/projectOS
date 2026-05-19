#ifndef DHCP_H
#define DHCP_H

#include "net.h"
#include "udp.h"

// DHCP magic cookie (host byte order for comparison after BSWAP32)
#define DHCP_MAGIC_COOKIE 0x63825363u

// Message types (option 53)
#define DHCP_MSG_DISCOVER 1
#define DHCP_MSG_OFFER    2
#define DHCP_MSG_REQUEST  3
#define DHCP_MSG_ACK      5
#define DHCP_MSG_NAK      6

// Option codes
#define DHCP_OPT_SUBNET_MASK  1
#define DHCP_OPT_ROUTER       3
#define DHCP_OPT_DNS_SERVER   6
#define DHCP_OPT_REQ_IP       50
#define DHCP_OPT_LEASE_TIME   51
#define DHCP_OPT_MSG_TYPE     53
#define DHCP_OPT_SERVER_ID    54
#define DHCP_OPT_PARAM_LIST   55
#define DHCP_OPT_END          255

typedef struct __attribute__((packed)) {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint8_t  ciaddr[4];
    uint8_t  yiaddr[4];
    uint8_t  siaddr[4];
    uint8_t  giaddr[4];
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;
    uint8_t  options[];
} dhcp_packet_t;

// Start the DHCP handshake on the given NIC.
// Polls the NIC internally; blocks until bound or timeout.
// Returns true if an IP was successfully leased.
bool dhcp_request_lease(net_nic_interfaces_t* nic);

// Called by udp_handle when a packet arrives on port 68.
void dhcp_handle(dhcp_packet_t* pkt, uint32_t length, net_nic_interfaces_t* nic);

// True once DHCP has successfully completed.
bool dhcp_is_bound(void);

#endif
