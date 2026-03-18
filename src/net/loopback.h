#ifndef LOOPBACK_H
#define LOOPBACK_H

#include "net.h"

// Loopback NIC is always present.
extern net_nic_interfaces_t nic_loopback;

// Register loopback with the network core.
void loopback_init(void);

#endif
