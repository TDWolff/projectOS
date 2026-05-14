#ifndef DNS_H
#define DNS_H

#include "../include/types.h"
#include "net.h"

#define DNS_SERVER_PORT 53
#define DNS_CLIENT_PORT 1053

// Resolve a hostname to an IPv4 address using a recursive DNS query.
// dns_server: optional override; pass NULL to use 8.8.8.8.
// Returns true and writes into out_ip[4] on success.
bool dns_resolve(const char* hostname, uint8_t out_ip[4],
                 net_nic_interfaces_t* nic, uint8_t dns_server[4]);

// Called by udp_handle when a UDP packet arrives on DNS_CLIENT_PORT.
void dns_handle_response(uint8_t* data, uint32_t len);

// Debug: number of packets delivered to dns_handle_response (regardless of match).
uint32_t dns_dbg_rx_count(void);
// Debug: number of packets where the transaction ID didn't match ours.
uint32_t dns_dbg_id_mismatch_count(void);

#endif
