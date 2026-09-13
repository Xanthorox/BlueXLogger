/*============================================================================
 * BlueXLogger - bxl_base64.c
 * Base64 / quoted-printable / RFC 2047 encoders.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "bxl_base64.h"

static const char k_b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int bxl_base64_encode(const void *data, size_t len, int wrap, BxlBuf *out)
{
    const bxl_u8 *p = (const bxl_u8 *)data;
    size_t i = 0;
    int    col = 0;

    if (!out) return BXL_FALSE;
    if (!data && len) return BXL_FALSE;

    if (!bxl_buf_reserve(out, ((len + 2) / 3) * 4 + ((wrap > 0) ? (len / 57 + 4) : 0) + 8))
        return BXL_FALSE;

    while (i + 2 < len) {
        bxl_u32 v = ((bxl_u32)p[i] << 16) | ((bxl_u32)p[i + 1] << 8) | p[i + 2];
        char q[4];
        q[0] = k_b64[(v >> 18) & 0x3F];
        q[1] = k_b64[(v >> 12) & 0x3F];
        q[2] = k_b64[(v >> 6) & 0x3F];
        q[3] = k_b64[v & 0x3F];
        if (wrap > 0) {
            for (int k = 0; k < 4; k++) {
                if (col >= wrap) { bxl_buf_appends(out, "\r\n"); col = 0; }
                bxl_buf_appendc(out, q[k]);
                col++;
            }
        } else {
            bxl_buf_append(out, q, 4);
        }
        i += 3;
    }

    if (i < len) {
        bxl_u32 v = (bxl_u32)p[i] << 16;
        int rem = (int)(len - i);
        char q[4];
        if (rem == 2) v |= (bxl_u32)p[i + 1] << 8;
        q[0] = k_b64[(v >> 18) & 0x3F];
        q[1] = k_b64[(v >> 12) & 0x3F];
        q[2] = (rem == 2) ? k_b64[(v >> 6) & 0x3F] : '=';
        q[3] = '=';
        for (int k = 0; k < 4; k++) {
            if (wrap > 0) {
                if (col >= wrap) { bxl_buf_appends(out, "\r\n"); col = 0; }
                bxl_buf_appendc(out, q[k]);
                col++;
            } else {
                bxl_buf_appendc(out, q[k]);
            }
        }
    }

    return BXL_TRUE;
}

char *bxl_base64_encode_str(const void *data, size_t len)
{
    BxlBuf b;
    char  *p;

    if (!bxl_buf_init(&b, len * 2 + 16)) return NULL;
    if (!bxl_base64_encode(data, len, 0, &b)) { bxl_buf_free(&b); return NULL; }
    p = bxl_buf_detach(&b, NULL);
    bxl_buf_free(&b);
    return p;
}

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int bxl_base64_decode(const char *in, size_t in_len, BxlBuf *out)
{
    bxl_u32 acc = 0;
    int     bits = 0;
    size_t  i;

    if (!in || !out) return BXL_FALSE;

    for (i = 0; i < in_len; i++) {
        char c = in[i];
        int  v;
        if (c == '=' ) break;
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        v = b64_val(c);
        if (v < 0) return BXL_FALSE;
        acc = (acc << 6) | (bxl_u32)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (!bxl_buf_appendc(out, (char)((acc >> bits) & 0xFF))) return BXL_FALSE;
        }
    }
    return BXL_TRUE;
}

/*==========================================================================
 * Quoted-printable
 *========================================================================*/
int bxl_quoted_printable_encode(const void *data, size_t len, BxlBuf *out)
{
    const bxl_u8 *p = (const bxl_u8 *)data;
    size_t i = 0;
    int    col = 0;

    if (!out) return BXL_FALSE;
    if (!data && len) return BXL_FALSE;

    while (i < len) {
        bxl_u8 c = p[i];
        char  enc[8];
        int   enc_len;
        int   is_last = (i + 1 == len);

        if (c == '\r' && i + 1 < len && p[i + 1] == '\n') {
            bxl_buf_appends(out, "\r\n");
            col = 0;
            i += 2;
            continue;
        }

        /* Literal-safe characters, but never a trailing space/tab on a line. */
        if (((c >= 33 && c <= 126) && c != '=') || c == ' ' || c == '\t') {
            if ((c == ' ' || c == '\t') && is_last) {
                enc_len = 3;
                enc[0] = '=';
                enc[1] = "0123456789ABCDEF"[(c >> 4) & 0xF];
                enc[2] = "0123456789ABCDEF"[c & 0xF];
                enc[3] = '\0';
            } else {
                enc[0] = (char)c;
                enc[1] = '\0';
                enc_len = 1;
            }
        } else {
            enc[0] = '=';
            enc[1] = "0123456789ABCDEF"[(c >> 4) & 0xF];
            enc[2] = "0123456789ABCDEF"[c & 0xF];
            enc[3] = '\0';
            enc_len = 3;
        }

        if (col + enc_len > 75) {
            bxl_buf_appends(out, "=\r\n");
            col = 0;
        }
        bxl_buf_append(out, enc, (size_t)enc_len);
        col += enc_len;
        i++;
    }
    return BXL_TRUE;
}

/*==========================================================================
 * RFC 2047 header encoding
 *========================================================================*/
static int header_needs_encoding(const char *v)
{
    const unsigned char *p = (const unsigned char *)v;
    size_t len = 0;
    for (; *p; p++, len++) {
        if (*p >= 0x80) return BXL_TRUE;
        if (*p == '\r' || *p == '\n') return BXL_TRUE;
    }
    return (len > 70) ? BXL_TRUE : BXL_FALSE;
}

int bxl_header_encode(const char *value, BxlBuf *out)
{
    BxlBuf raw;

    if (!value || !out) return BXL_FALSE;

    /* Strip CR/LF unconditionally - header injection defence. */
    if (!bxl_buf_init(&raw, strlen(value) + 1)) return BXL_FALSE;
    for (const char *p = value; *p; p++) {
        if (*p == '\r' || *p == '\n') continue;
        bxl_buf_appendc(&raw, *p);
    }

    if (!header_needs_encoding(raw.data)) {
        int ok = bxl_buf_append(out, raw.data, raw.len);
        bxl_buf_free(&raw);
        return ok;
    }

    {
        int ok = bxl_buf_appends(out, "=?UTF-8?B?");
        if (ok) ok = bxl_base64_encode(raw.data, raw.len, 0, out);
        if (ok) ok = bxl_buf_appends(out, "?=");
        bxl_buf_free(&raw);
        return ok;
    }
}
