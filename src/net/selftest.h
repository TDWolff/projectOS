#ifndef NET_SELFTEST_H
#define NET_SELFTEST_H

#include "../include/types.h"

// Runs a small networking self-test suite (loopback-only for now).
// Returns true if all tests pass.
bool net_selftest_run(void);

#endif
