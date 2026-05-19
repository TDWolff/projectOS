#include "tls.h"
#include "../cpu/entropy.h"
#include "../mem/heap.h"
#include "../lib/string.h"
#include "../drivers/net/e1000.h"
#include "../fs/initrd.h"

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"

struct tls_conn {
    tcp_conn_t*              tcp;
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt         ca_cert;
};

/* mbedTLS BIO send callback */
static int tls_bio_send(void* ctx, const unsigned char* buf, size_t len) {
    tcp_conn_t* tcp = (tcp_conn_t*)ctx;
    uint32_t sent = tcp_send(tcp, (const uint8_t*)buf, (uint32_t)len);
    if (sent == 0) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return (int)sent;
}

/* mbedTLS BIO recv callback */
static int tls_bio_recv(void* ctx, unsigned char* buf, size_t len) {
    tcp_conn_t* tcp = (tcp_conn_t*)ctx;
    for (int i = 0; i < 64; i++) e1000_poll();
    uint32_t got = tcp_recv(tcp, (uint8_t*)buf, (uint32_t)len);
    if (got == 0) {
        if (!tcp_is_connected(tcp)) return MBEDTLS_ERR_SSL_CONN_EOF;
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return (int)got;
}

tls_conn_t* tls_connect(const char* hostname, uint8_t ip[4], uint16_t port,
                        net_nic_interfaces_t* nic) {
    /* Load CA bundle from initrd (ca_certs.pem in osstorage) */
    file_t* ca_file = initrd_open("ca_certs.pem");
    if (!ca_file) {
        /* No CA bundle found — fall back to no verification */
        ca_file = (void*)0;
    }

    tls_conn_t* c = (tls_conn_t*)kmalloc(sizeof(tls_conn_t));
    if (!c) return (void*)0;
    memset(c, 0, sizeof(*c));

    /* 1. TCP connection */
    c->tcp = tcp_connect(ip, port, nic);
    if (!c->tcp) { kfree(c); return (void*)0; }

    /* 2. Seed RNG */
    mbedtls_entropy_init(&c->entropy);
    mbedtls_ctr_drbg_init(&c->drbg);
    const char* pers = "projectos_tls";
    int ret = mbedtls_ctr_drbg_seed(&c->drbg, mbedtls_entropy_func, &c->entropy,
                                     (const unsigned char*)pers, 13);
    if (ret != 0) goto fail;

    /* 3. Load CA certificates */
    mbedtls_x509_crt_init(&c->ca_cert);
    if (ca_file) {
        /* PEM must be null-terminated for mbedTLS */
        const unsigned char* pem = (const unsigned char*)ca_file->address;
        size_t pem_len = (size_t)ca_file->size;
        /* mbedtls_x509_crt_parse expects null-terminated PEM or DER.
           The initrd data is a raw PEM file — add 1 to len for the null byte
           only if mbedTLS needs it (it checks for a null at the end). */
        ret = mbedtls_x509_crt_parse(&c->ca_cert, pem, pem_len + 1);
        /* ret < 0 = fatal; ret > 0 = some certs failed (still usable) */
    }

    /* 4. TLS config */
    mbedtls_ssl_config_init(&c->conf);
    ret = mbedtls_ssl_config_defaults(&c->conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) goto fail;

    if (ca_file) {
        mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&c->conf, &c->ca_cert, (void*)0);
    } else {
        mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_NONE);
    }
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->drbg);

    /* 5. SSL context */
    mbedtls_ssl_init(&c->ssl);
    ret = mbedtls_ssl_setup(&c->ssl, &c->conf);
    if (ret != 0) goto fail;

    ret = mbedtls_ssl_set_hostname(&c->ssl, hostname);
    if (ret != 0) goto fail;

    mbedtls_ssl_set_bio(&c->ssl, c->tcp, tls_bio_send, tls_bio_recv, (void*)0);

    /* 6. Handshake */
    do {
        ret = mbedtls_ssl_handshake(&c->ssl);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            for (int i = 0; i < 256; i++) e1000_poll();
        } else if (ret != 0) {
            goto fail;
        }
    } while (ret != 0);

    return c;

fail:
    tcp_close(c->tcp);
    tcp_free(c->tcp);
    kfree(c);
    return (void*)0;
}

int tls_send(tls_conn_t* conn, const uint8_t* data, uint32_t len) {
    int ret;
    uint32_t sent = 0;
    while (sent < len) {
        ret = mbedtls_ssl_write(&conn->ssl, data + sent, len - sent);
        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            for (int i = 0; i < 64; i++) e1000_poll();
            continue;
        }
        if (ret <= 0) return ret;
        sent += (uint32_t)ret;
    }
    return (int)sent;
}

int tls_recv(tls_conn_t* conn, uint8_t* buf, uint32_t max_len) {
    int ret;
    do {
        ret = mbedtls_ssl_read(&conn->ssl, buf, max_len);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
            for (int i = 0; i < 256; i++) e1000_poll();
        } else {
            break;
        }
    } while (1);

    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0) return 0;
    return ret;
}

void tls_close(tls_conn_t* conn) {
    mbedtls_ssl_close_notify(&conn->ssl);
    tcp_close(conn->tcp);
    tcp_free(conn->tcp);
    mbedtls_ssl_free(&conn->ssl);
    mbedtls_ssl_config_free(&conn->conf);
    mbedtls_ctr_drbg_free(&conn->drbg);
    mbedtls_entropy_free(&conn->entropy);
    mbedtls_x509_crt_free(&conn->ca_cert);
    kfree(conn);
}
