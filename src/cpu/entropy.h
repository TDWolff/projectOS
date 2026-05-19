#ifndef ENTROPY_H
#define ENTROPY_H

#include "../include/types.h"

/* Returns a 64-bit random value via RDRAND; returns 0 on failure. */
uint64_t rdrand64(void);

/* mbedTLS hardware entropy callback — registered via MBEDTLS_ENTROPY_HARDWARE_ALT */
int mbedtls_hardware_poll(void* data, unsigned char* output, size_t len, size_t* olen);

#endif
