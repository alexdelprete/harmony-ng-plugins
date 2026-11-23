/* TLS client for plugins that talk to LAN devices with self-signed certificates (TVs): any
 * certificate is taken on the first connection and its public key is pinned (trust on first
 * use); a later connection that shows another key is refused. Uses the Hub's shared
 * libbearssl.so.0 (manifest: "requires_libs": ["libbearssl.so.0"]); only names that library
 * exports, or inline BearSSL functions, may be called here. Header only, needs plugin.h. */
#ifndef HNG_TLS_H
#define HNG_TLS_H

#include "plugin.h"
#include "bearssl.h"

typedef struct {
    const br_x509_class *vtable;
    br_x509_decoder_context dec;
    br_x509_pkey pkey;
    unsigned char key[600]; /* RSA modulus or EC point of the server's certificate */
    size_t key_len;
    unsigned char rsa_e[8];
    size_t e_len;
    int first;
    char expect[65];        /* the pinned SHA-256 of key (hex), "" to take any */
    char got[65];
} tls_x509_t;

typedef struct {
    int fd;
    br_ssl_client_context sc;
    br_sslio_context ioc;
    tls_x509_t x;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
} tls_conn_t;

PLG_FN void tls_x_start_chain(const br_x509_class **ctx, const char *server_name) {
    (void)server_name;
    tls_x509_t *x = (tls_x509_t *)ctx;
    x->first = 1;
    x->key_len = 0;
    x->got[0] = '\0';
}

PLG_FN void tls_x_start_cert(const br_x509_class **ctx, uint32_t length) {
    (void)length;
    tls_x509_t *x = (tls_x509_t *)ctx;
    if (x->first) br_x509_decoder_init(&x->dec, 0, 0);
}

PLG_FN void tls_x_append(const br_x509_class **ctx, const unsigned char *buf, size_t len) {
    tls_x509_t *x = (tls_x509_t *)ctx;
    if (x->first) br_x509_decoder_push(&x->dec, buf, len);
}

PLG_FN void tls_x_end_cert(const br_x509_class **ctx) {
    tls_x509_t *x = (tls_x509_t *)ctx;
    if (!x->first) return;
    x->first = 0;
    const br_x509_pkey *pk = br_x509_decoder_last_error(&x->dec) == 0 ? br_x509_decoder_get_pkey(&x->dec) : NULL;
    if (!pk) return;
    x->pkey = *pk;
    if (pk->key_type == BR_KEYTYPE_RSA && pk->key.rsa.nlen <= sizeof(x->key) && pk->key.rsa.elen <= sizeof(x->rsa_e)) {
        memcpy(x->key, pk->key.rsa.n, pk->key.rsa.nlen);
        x->key_len = pk->key.rsa.nlen;
        memcpy(x->rsa_e, pk->key.rsa.e, pk->key.rsa.elen);
        x->e_len = pk->key.rsa.elen;
        x->pkey.key.rsa.n = x->key;
        x->pkey.key.rsa.e = x->rsa_e;
    } else if (pk->key_type == BR_KEYTYPE_EC && pk->key.ec.qlen <= sizeof(x->key)) {
        memcpy(x->key, pk->key.ec.q, pk->key.ec.qlen);
        x->key_len = pk->key.ec.qlen;
        x->pkey.key.ec.q = x->key;
    }
}

PLG_FN unsigned tls_x_end_chain(const br_x509_class **ctx) {
    tls_x509_t *x = (tls_x509_t *)ctx;
    if (!x->key_len) return BR_ERR_X509_UNSUPPORTED; /* no RSA or EC key: refuse */
    unsigned char h[32];
    br_sha256_context hc;
    br_sha256_init(&hc);
    br_sha256_update(&hc, x->key, x->key_len);
    br_sha256_out(&hc, h);
    for (int i = 0; i < 32; i++) snprintf(x->got + 2 * i, 3, "%02x", h[i]);
    if (x->expect[0] && strcmp(x->expect, x->got) != 0) return BR_ERR_X509_NOT_TRUSTED;
    return 0;
}

PLG_FN const br_x509_pkey *tls_x_get_pkey(const br_x509_class *const *ctx, unsigned *usages) {
    tls_x509_t *x = (tls_x509_t *)ctx;
    if (usages) *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
    return &x->pkey;
}

static const br_x509_class tls_x_vtable __attribute__((unused)) = {
    sizeof(tls_x509_t), tls_x_start_chain, tls_x_start_cert, tls_x_append,
    tls_x_end_cert, tls_x_end_chain, tls_x_get_pkey
};

PLG_FN int tls_sock_read(void *ctx, unsigned char *buf, size_t len) {
    for (;;) {
        ssize_t n = recv(*(int *)ctx, buf, len, 0);
        if (n < 0 && errno == EINTR) continue;
        return n <= 0 ? -1 : (int)n;
    }
}

PLG_FN int tls_sock_write(void *ctx, const unsigned char *buf, size_t len) {
    for (;;) {
        ssize_t n = send(*(int *)ctx, buf, len, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        return n <= 0 ? -1 : (int)n;
    }
}

/* TLS 1.0 to 1.2, the suites TVs offer (ECDHE or RSA key exchange, AES-GCM, AES-CBC,
 * ChaCha20), with the library's default implementations */
PLG_FN void tls_client_init(br_ssl_client_context *cc) {
    static const uint16_t suites[] = {
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384, BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
        BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256, BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256, BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA, BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
        BR_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA, BR_TLS_RSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_RSA_WITH_AES_256_GCM_SHA384, BR_TLS_RSA_WITH_AES_128_CBC_SHA256,
        BR_TLS_RSA_WITH_AES_128_CBC_SHA, BR_TLS_RSA_WITH_AES_256_CBC_SHA,
    };
    br_ssl_client_zero(cc);
    br_ssl_engine_set_versions(&cc->eng, BR_TLS10, BR_TLS12);
    br_ssl_engine_set_suites(&cc->eng, suites, sizeof(suites) / sizeof(suites[0]));
    br_ssl_client_set_default_rsapub(cc);
    br_ssl_engine_set_default_rsavrfy(&cc->eng);
    br_ssl_engine_set_default_ecdsa(&cc->eng);
    br_ssl_engine_set_default_ec(&cc->eng);
    br_ssl_engine_set_hash(&cc->eng, br_md5_ID, &br_md5_vtable);
    br_ssl_engine_set_hash(&cc->eng, br_sha1_ID, &br_sha1_vtable);
    br_ssl_engine_set_hash(&cc->eng, br_sha224_ID, &br_sha224_vtable);
    br_ssl_engine_set_hash(&cc->eng, br_sha256_ID, &br_sha256_vtable);
    br_ssl_engine_set_hash(&cc->eng, br_sha384_ID, &br_sha384_vtable);
    br_ssl_engine_set_prf10(&cc->eng, &br_tls10_prf);
    br_ssl_engine_set_prf_sha256(&cc->eng, &br_tls12_sha256_prf);
    br_ssl_engine_set_prf_sha384(&cc->eng, &br_tls12_sha384_prf);
    br_ssl_engine_set_default_aes_gcm(&cc->eng);
    br_ssl_engine_set_default_aes_cbc(&cc->eng);
    br_ssl_engine_set_default_chapol(&cc->eng);
}

/* TLS over the connected socket fd (taken over: closed by tls_close). The server's key is
 * checked against pin_path; with no pin yet it is taken and written there. 0 when the
 * handshake completed. */
PLG_FN int tls_open(tls_conn_t *t, int fd, const char *pin_path) {
    memset(&t->x, 0, sizeof(t->x));
    t->x.vtable = &tls_x_vtable;
    if (pin_path) plg_read_file(pin_path, t->x.expect, sizeof(t->x.expect));
    t->fd = fd;
    tls_client_init(&t->sc);
    br_ssl_engine_set_x509(&t->sc.eng, &t->x.vtable);
    br_ssl_engine_set_buffer(&t->sc.eng, t->iobuf, sizeof(t->iobuf), 1);
    br_ssl_client_reset(&t->sc, NULL, 0);
    br_sslio_init(&t->ioc, &t->sc.eng, tls_sock_read, &t->fd, tls_sock_write, &t->fd);
    if (br_sslio_flush(&t->ioc) != 0) {
        int err = br_ssl_engine_last_error(&t->sc.eng);
        if (err == BR_ERR_X509_NOT_TRUSTED) {
            printf("[-] the device shows another key than the one pinned in %s: refused (pair again to trust "
                   "the new key)\n", pin_path ? pin_path : "");
        } else {
            printf("[-] TLS handshake failed (BearSSL error %d)\n", err);
        }
        close(fd);
        t->fd = -1;
        return -1;
    }
    if (pin_path && !t->x.expect[0] && plg_write_file(pin_path, t->x.got, 0644) == 0) {
        printf("[*] pinned the device's key in %s\n", pin_path);
    }
    return 0;
}

PLG_FN void tls_close(tls_conn_t *t) {
    if (t->fd < 0) return;
    br_ssl_engine_close(&t->sc.eng);
    close(t->fd);
    t->fd = -1;
}

PLG_FN int tls_write(tls_conn_t *t, const void *buf, size_t len) {
    return br_sslio_write_all(&t->ioc, buf, len) == 0 && br_sslio_flush(&t->ioc) == 0 ? 0 : -1;
}

PLG_FN int tls_read(tls_conn_t *t, void *buf, size_t len) {
    return br_sslio_read_all(&t->ioc, buf, len);
}

/* Decrypted bytes BearSSL already holds: poll() on the socket does not see them */
PLG_FN int tls_pending(tls_conn_t *t) {
    return t->fd >= 0 && (br_ssl_engine_current_state(&t->sc.eng) & BR_SSL_RECVAPP) != 0;
}

#endif
