// Copyright (C) 2013 - Will Glozer.  All rights reserved.

#include <pthread.h>

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "ssl.h"

int ssl_data_index;
u_char *ssl_protocol, *ssl_cipher;

static int ssl_new_client_session(SSL *ssl, SSL_SESSION *session) {
    connection *c = SSL_get_ex_data(ssl, ssl_data_index);

    if (c->cache) {
        if (c->cache->cached_session) {
            SSL_SESSION_free(c->cache->cached_session);
            c->cache->cached_session = NULL;
        }
        c->cache->cached_session = session;
    }

    return 1;
}

SSL_CTX *ssl_init(int ssl_proto_version, char *ssl_cipher, bool tls_session_reuse) {
    SSL_CTX *ctx = NULL;

    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    ssl_data_index = SSL_get_ex_new_index(0, NULL, NULL, NULL, NULL);

    ctx = SSL_CTX_new(SSLv23_method());

    if (ctx == NULL) {
        return NULL;
    }

    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    SSL_CTX_set_verify_depth(ctx, 0);
    SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);

    if (ssl_proto_version == 0) {
#ifdef SSL_CTX_set_min_proto_version
        SSL_CTX_set_min_proto_version(ctx, 0);
#ifdef TLS1_3_VERSION
        SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
#else
        SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
#endif
#endif

    } else {
#ifdef SSL_CTX_set_min_proto_version
        SSL_CTX_set_min_proto_version(ctx, ssl_proto_version);
        SSL_CTX_set_max_proto_version(ctx, ssl_proto_version);
#endif
    }

    if (ssl_cipher != NULL) {
        if (!SSL_CTX_set_cipher_list(ctx, ssl_cipher)) {
            fprintf(stderr, "error setting cipher list [%s]\n", ssl_cipher);
            ERR_print_errors_fp(stderr);
            exit(1);
        }
    }

    if (tls_session_reuse) {
        SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL);
        SSL_CTX_sess_set_new_cb(ctx, ssl_new_client_session);
    }

    return ctx;
}

status ssl_connect(connection *c, char *host) {
    int r;

    if (SSL_get_fd(c->ssl) != c->fd && c->cache && c->cache->cached_session) {
        SSL_set_session(c->ssl, c->cache->cached_session);
    }

    SSL_set_fd(c->ssl, c->fd);
    SSL_set_tlsext_host_name(c->ssl, host);
    if ((r = SSL_connect(c->ssl)) != 1) {
        switch (SSL_get_error(c->ssl, r)) {
            case SSL_ERROR_WANT_READ:  return RETRY;
            case SSL_ERROR_WANT_WRITE: return RETRY;
            default:                   return ERROR;
        }
    }

    /* assuming there will be the same proto/cipher for all subsequent connections */
    if (ssl_protocol == NULL) {
        ssl_protocol = (u_char *) SSL_get_version(c->ssl);
        ssl_cipher = (u_char *) SSL_get_cipher_name(c->ssl);
    }

    return OK;
}

status ssl_close(connection *c) {
    SSL_shutdown(c->ssl);
    SSL_clear(c->ssl);
    SSL_free(c->ssl);
    c->ssl = NULL;
    return OK;
}

status ssl_read(connection *c, size_t *n) {
    int r;
    if ((r = SSL_read(c->ssl, c->buf, sizeof(c->buf))) <= 0) {
        switch (SSL_get_error(c->ssl, r)) {
            case SSL_ERROR_WANT_READ:  return RETRY;
            case SSL_ERROR_WANT_WRITE: return RETRY;
            default:                   return ERROR;
        }
    }
    *n = (size_t) r;
    return OK;
}

status ssl_write(connection *c, char *buf, size_t len, size_t *n) {
    int r;
    if ((r = SSL_write(c->ssl, buf, len)) <= 0) {
        switch (SSL_get_error(c->ssl, r)) {
            case SSL_ERROR_WANT_READ:  return RETRY;
            case SSL_ERROR_WANT_WRITE: return RETRY;
            default:                   return ERROR;
        }
    }
    *n = (size_t) r;
    return OK;
}

size_t ssl_readable(connection *c) {
    return SSL_pending(c->ssl);
}
