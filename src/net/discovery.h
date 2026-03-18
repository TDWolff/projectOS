#ifndef NET_DISCOVERY_H
#define NET_DISCOVERY_H

#include "net.h"
#include "../include/types.h"

// Simple NIC discovery/selection.
//
// Contract:
// - After drivers call `net_register_nic()`, call `net_discovery_run()` to pick
//   a primary NIC for the stack (typically the first non-loopback NIC that is UP).
// - If no suitable NIC exists, primary falls back to loopback.
//
// This is intentionally tiny and centralized so you don't need a discovery
// script per device.

void net_discovery_run(void);

// Returns the currently-selected primary NIC.
// Never returns NULL once net_init+net_discovery_run have completed.
net_nic_interfaces_t* net_get_primary_nic(void);

// Returns true if a primary NIC has been selected.
bool net_has_primary_nic(void);

#endif
