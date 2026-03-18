#ifndef E1000_H
#define E1000_H

#include "../../include/types.h"

// Minimal e1000 bring-up for QEMU's e1000:
// - Find device via PCI
// - Map BAR0 MMIO
// - Read MAC
// - Register net_nic_interfaces_t

bool e1000_init(void);

#endif
