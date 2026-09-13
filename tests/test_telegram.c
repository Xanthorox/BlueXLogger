/*============================================================================
 * BlueXLogger - tests/test_telegram.c
 * Telegram Bot API channel: token/chat validation, config bridging, the retry
 * policy and the failure-stage mapping.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * The transport is HTTPS and Telegram's endpoint is the only one that speaks
 * it, so these cases do not try to fake a successful API call. Instead they
 * pin everything that decides whether a call is even attempted, and they drive
 * a real connect against a port that is known to be closed - which is exactly
 * the "no network" path an operator hits on a locked-down machine.
 *==========================================================================*/
#include "tests.h"
#include "bxl_telegram.h"
#include "bxl_net.h"
#include "bxl_util.h"

#include <winsock2.h>
#include <ws2tcpip.h>

/*----------------------------------------------------------------------------
 * Token shape
 *--------------------------------------------------------------------------*/
static void t_token(void)
{
    /* A realistic-shaped token: 10-digit id, 35 characters of body. */
    static const char *good =
        "8123456789:AAHdqTcvCH1vGWJxfSeofSAs0K5PALDsaw";

    t_begin("a well-formed bot token is accepted");
    T_OK(bxl_telegram_token_ok(good));

    t_begin("an empty token is rejected");
    T_OK(!bxl_telegram_token_ok(""));
    T_OK(!bxl_telegram_token_ok(NULL));

    t_begin("the bot's @username is rejected");
    T_OK(!bxl_telegram_token_ok("@MyBot"));

    t_begin("a token without a colon is rejected");
    T_OK(!bxl_telegram_token_ok("8123456789AAHdqTcvCH1vGWJxfSeofSAs0K5"));

    t_begin("a token with too few leading digits is rejected");
    T_OK(!bxl_telegram_token_ok("123:AAHdqTcvCH1vGWJxfSeofSAs0K5PALDsaw"));

    t_begin("a token whose body is too short is rejected");
    T_OK(!bxl_telegram_token_ok("8123456789:short"));

    t_begin("a body containing punctuation is rejected");
    T_OK(!bxl_telegram_token_ok(
             "8123456789:AAHdqTcvCH1vGWJxfSeofSAs0K5PALD!aw"));

    t_begin("an underscore and hyphen in the body are accepted");
    T_OK(bxl_telegram_token_ok(
             "8123456789:AAH_dqTcv-CH1vGWJxfSeofSAs0K5PALDs"));
}

/*----------------------------------------------------------------------------
 * Chat id shape
 *--------------------------------------------------------------------------*/
static void t_chat(void)
{
    t_begin("a positive numeric chat id is accepted");
    T_OK(bxl_telegram_chat_ok("123456789"));

    t_begin("a negative (group) chat id is accepted");
    T_OK(bxl_telegram_chat_ok("-1001234567890"));

    t_begin("an @channel name is accepted");
    T_OK(bxl_telegram_chat_ok("@my_channel"));

    t_begin("an empty chat id is rejected");
    T_OK(!bxl_telegram_chat_ok(""));
    T_OK(!bxl_telegram_chat_ok(NULL));

    t_begin("a bare @ is rejected");
    T_OK(!bxl_telegram_chat_ok("@"));

    t_begin("a too-short channel name is rejected");
    T_OK(!bxl_telegram_chat_ok("@abc"));

    t_begin("a channel name with a space is rejected");
    T_OK(!bxl_telegram_chat_ok("@my channel"));

    t_begin("a numeric id with a stray letter is rejected");
    T_OK(!bxl_telegram_chat_ok("12345a"));

    t_begin("a lone minus sign is rejected");
    T_OK(!bxl_telegram_chat_ok("-"));
}

/*----------------------------------------------------------------------------
 * Redaction
 *--------------------------------------------------------------------------*/
static void t_redact(void)
{
    static const char *tok =
        "8123456789:AAHdqTcvCH1vGWJxfSeofSAs0K5PALDsaw";
    char out[128];

    t_begin("a token is redacted to its bot id and a prefix");
    bxl_telegram_redact(tok, out, sizeof(out));
    T_OK(strncmp(out, "8123456789:", 11) == 0);

    t_begin("the redaction never contains the secret body");
    T_OK(strstr(out, "AAHdqTcvCH1vGWJxfSeofSAs0K5PALDsaw") == NULL);
    T_OK(strstr(out, "CH1vGWJx") == NULL);

    t_begin("a malformed token still produces something printable");
    bxl_telegram_redact("not-a-token", out, sizeof(out));
    T_OK(out[0] != 0);

    t_begin("an empty token does not crash or produce garbage");
    bxl_telegram_redact("", out, sizeof(out));
    T_STR(out, "(empty)");

    t_begin("a NULL token renders the same placeholder as an empty one");
    bxl_telegram_redact(NULL, out, sizeof(out));
    T_STR(out, "(empty)");

    t_begin("a NULL output buffer is ignored rather than dereferenced");
    bxl_telegram_redact("not-a-token", NULL, 0);
    T_OK(1);
}

/*----------------------------------------------------------------------------
 * Config bridging
 *--------------------------------------------------------------------------*/
static void t_config_bridge(void)
{
    BxlConfig         cfg;
    BxlTelegramConfig tc;

    t_begin("the operator configuration bridges into the Telegram config");
    bxl_config_defaults(&cfg);
    bxl_str_copy(cfg.tg_bot_token, sizeof(cfg.tg_bot_token),
                 "8123456789:AAHdqTcvCH1vGWJxfSeofSAs0K5PALDsaw");
    bxl_str_copy(cfg.tg_chat_id, sizeof(cfg.tg_chat_id), "-1001234567890");
    cfg.tg_parse_html       = 0;
    cfg.tg_send_screenshots = 0;
    cfg.tg_full_log_file    = 1;

    bxl_telegram_config_from(&cfg, &tc);
    T_STR(tc.bot_token, "8123456789:AAHdqTcvCH1vGWJxfSeofSAs0K5PALDsaw");
    T_STR(tc.chat_id, "-1001234567890");
    T_INT(tc.parse_html, 0);
    T_INT(tc.send_screenshots, 0);
    T_INT(tc.full_log_file, 1);

    t_begin("the API endpoint defaults to api.telegram.org:443");
    T_STR(tc.api_host, "api.telegram.org");
    T_INT(tc.api_port, 443);

    t_begin("a NULL configuration bridges to a safe zeroed config");
    memset(&tc, 0xAA, sizeof(tc));
    bxl_telegram_config_from(NULL, &tc);
    T_INT(tc.api_port, 0);
    T_STR(tc.bot_token, "");
}

/*----------------------------------------------------------------------------
 * Retry policy
 *--------------------------------------------------------------------------*/
static void t_retry_policy(void)
{
    BxlTelegramResult res;

    memset(&res, 0, sizeof(res));

    t_begin("a transport failure is retryable");
    res.stage = BXL_TG_STAGE_CONNECT;
    T_OK(bxl_telegram_retryable(&res));
    res.stage = BXL_TG_STAGE_TLS;
    T_OK(bxl_telegram_retryable(&res));
    res.stage = BXL_TG_STAGE_SEND;
    T_OK(bxl_telegram_retryable(&res));

    t_begin("a rate limit is retryable");
    memset(&res, 0, sizeof(res));
    res.stage = BXL_TG_STAGE_RATE_LIMIT;
    T_OK(bxl_telegram_retryable(&res));

    t_begin("a 5xx from the API is retryable");
    memset(&res, 0, sizeof(res));
    res.stage = BXL_TG_STAGE_API;
    res.http_status = 502;
    T_OK(bxl_telegram_retryable(&res));

    t_begin("an api_code of 429 is retryable even on a 200 envelope");
    memset(&res, 0, sizeof(res));
    res.stage = BXL_TG_STAGE_API;
    res.http_status = 200;
    res.api_code = 429;
    T_OK(bxl_telegram_retryable(&res));

    t_begin("a 400 is not retryable");
    memset(&res, 0, sizeof(res));
    res.stage = BXL_TG_STAGE_API;
    res.http_status = 400;
    res.api_code = 400;
    T_OK(!bxl_telegram_retryable(&res));

    t_begin("a 403 is not retryable");
    memset(&res, 0, sizeof(res));
    res.stage = BXL_TG_STAGE_API;
    res.http_status = 403;
    T_OK(!bxl_telegram_retryable(&res));

    t_begin("a NULL result is not retryable");
    T_OK(!bxl_telegram_retryable(NULL));
}

/*----------------------------------------------------------------------------
 * Stage names
 *--------------------------------------------------------------------------*/
static void t_stage_names(void)
{
    t_begin("every stage has a human name");
    T_STR_W(bxl_telegram_stage_name(BXL_TG_STAGE_CONNECT), L"connect");
    T_STR_W(bxl_telegram_stage_name(BXL_TG_STAGE_TLS), L"TLS");
    T_STR_W(bxl_telegram_stage_name(BXL_TG_STAGE_SEND), L"send");
    T_STR_W(bxl_telegram_stage_name(BXL_TG_STAGE_RESPONSE), L"response");
    T_STR_W(bxl_telegram_stage_name(BXL_TG_STAGE_PARSE), L"reply parsing");
    T_STR_W(bxl_telegram_stage_name(BXL_TG_STAGE_API), L"Bot API");
    T_STR_W(bxl_telegram_stage_name(BXL_TG_STAGE_RATE_LIMIT), L"rate limit");
    T_STR_W(bxl_telegram_stage_name(BXL_TG_STAGE_NONE), L"not started");
}

/*----------------------------------------------------------------------------
 * A real connect attempt against a closed port
 *--------------------------------------------------------------------------*/
static int closed_port(void)
{
    SOCKET s;
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);
    int port = 0;

    bxl_net_startup();
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        getsockname(s, (struct sockaddr *)&addr, &addrlen) == 0) {
        port = ntohs(addr.sin_port);
    }
    closesocket(s);
    return port;
}

static void t_unreachable_endpoint(void)
{
    BxlTelegramConfig tc;
    BxlTelegramResult res;
    BxlConfig         cfg;
    int               port = closed_port();

    t_begin("a closed loopback port can be found for the test");
    T_OK(port > 0);
    if (port <= 0) return;

    bxl_config_defaults(&cfg);
    bxl_str_copy(cfg.tg_bot_token, sizeof(cfg.tg_bot_token),
                 "8123456789:AAHdqTcvCH1vGWJxfSeofSAs0K5PALDsaw");
    bxl_str_copy(cfg.tg_chat_id, sizeof(cfg.tg_chat_id), "-1001234567890");
    bxl_telegram_config_from(&cfg, &tc);

    /* Point the transport at the dead port so the connect fails immediately
     * rather than reaching the real API. */
    StringCchPrintfA(tc.api_host, sizeof(tc.api_host), "127.0.0.1");
    tc.api_port      = port;
    tc.timeout_ms    = 1500;
    tc.max_attempts  = 1;
    tc.retry_base_ms = 0;

    t_begin("getMe against a dead endpoint fails with a diagnostic");
    memset(&res, 0, sizeof(res));
    T_OK(!bxl_telegram_verify(&tc, &res));
    T_OK(res.error[0] != 0);
    T_OK(res.stage == BXL_TG_STAGE_CONNECT);
    T_OK(bxl_telegram_retryable(&res));

    t_begin("sendMessage against a dead endpoint fails at the connect stage");
    memset(&res, 0, sizeof(res));
    T_OK(!bxl_telegram_send_text(&tc, "hello", &res));
    T_OK(res.stage == BXL_TG_STAGE_CONNECT);
    T_OK(res.http_status == 0);

    t_begin("a malformed token is rejected before any socket is opened");
    memset(&res, 0, sizeof(res));
    bxl_str_copy(tc.bot_token, sizeof(tc.bot_token), "@not_a_token");
    T_OK(!bxl_telegram_verify(&tc, &res));
    T_OK(res.stage == BXL_TG_STAGE_NONE);

    bxl_net_cleanup();
}

void test_telegram(void)
{
    t_suite("telegram");
    t_token();
    t_chat();
    t_redact();
    t_config_bridge();
    t_retry_policy();
    t_stage_names();
    t_unreachable_endpoint();
}
