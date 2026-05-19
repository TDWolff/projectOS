#ifndef TLS_H
#define TLS_H

#include "../include/types.h"
#include "tcp.h"

/* Opaque TLS connection handle */
typedef struct tls_conn tls_conn_t;

/* Connect to host:port over TLS (TCP + TLS handshake).
   hostname is used for SNI and certificate CN/SAN verification.
   Returns NULL on failure. */
tls_conn_t* tls_connect(const char* hostname, uint8_t ip[4], uint16_t port,
                        net_nic_interfaces_t* nic);

/* Send plaintext bytes. Returns bytes sent, <0 on error. */
int tls_send(tls_conn_t* conn, const uint8_t* data, uint32_t len);

/* Receive plaintext bytes into buf (up to max_len).
   Returns bytes received, 0 on clean close, <0 on error. */
int tls_recv(tls_conn_t* conn, uint8_t* buf, uint32_t max_len);

/* Close and free the TLS connection. */
void tls_close(tls_conn_t* conn);

#endif /* TLS_H */
