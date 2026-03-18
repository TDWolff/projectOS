#include "discovery.h"

#include "loopback.h"

static net_nic_interfaces_t* g_net_primary = 0;

static bool nic_is_loopback(const net_nic_interfaces_t* nic) {
    if (!nic) return false;
    return (nic->flags & IFF_LOOPBACK) != 0;
}

static bool nic_is_usable(const net_nic_interfaces_t* nic) {
    if (!nic) return false;
    if (!nic->send_packet) return false;
    if ((nic->flags & IFF_UP) == 0) return false;
    if ((nic->flags & IFF_RUNNING) == 0) return false;
    return true;
}

void net_discovery_run(void) {
    // Prefer the first usable non-loopback NIC.
    uint32_t n = net_get_nic_count();
    for (uint32_t i = 0; i < n; i++) {
        net_nic_interfaces_t* nic = net_get_nic(i);
        if (!nic) continue;
        if (nic_is_loopback(nic)) continue;
        if (!nic_is_usable(nic)) continue;

        g_net_primary = nic;
        return;
    }

    // Fall back to loopback.
    g_net_primary = &nic_loopback;
}

net_nic_interfaces_t* net_get_primary_nic(void) {
    if (!g_net_primary) {
        // Even if discovery hasn't run yet, never return NULL.
        return &nic_loopback;
    }
    return g_net_primary;
}

bool net_has_primary_nic(void) {
    return g_net_primary != 0;
}
