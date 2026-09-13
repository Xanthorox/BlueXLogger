/*============================================================================
 * BlueXLogger - tests/test_http.c
 * HTTPS client helpers: JSON readers and the multipart/form-data builder.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * The transport itself is exercised by the Telegram suite (which drives a real
 * connect attempt). These cases pin the pure functions the Bot API depends on:
 * a mis-parsed "description" would turn a precise API error into "HTTP 400",
 * and a malformed multipart body would make every sendPhoto fail with an
 * opaque 400 from Telegram.
 *==========================================================================*/
#include "tests.h"
#include "bxl_http.h"
#include "bxl_util.h"

#include <stdlib.h>

/* memmem is a GNU extension and does not exist in the MSVC CRT, so the one
 * place the suite needs it gets its own ten-line version. */
static const void *find_bytes(const void *hay, size_t hay_len,
                              const void *needle, size_t needle_len)
{
    const unsigned char *h = (const unsigned char *)hay;
    const unsigned char *n = (const unsigned char *)needle;
    size_t i;

    if (needle_len == 0 || hay_len < needle_len) return NULL;
    for (i = 0; i + needle_len <= hay_len; i++)
        if (memcmp(h + i, n, needle_len) == 0) return h + i;
    return NULL;
}

/*----------------------------------------------------------------------------
 * JSON readers
 *--------------------------------------------------------------------------*/
static void t_json_strings(void)
{
    char out[256];

    t_begin("a plain string value is extracted");
    T_OK(bxl_json_get_string("{\"ok\":true,\"description\":\"Bad Request\"}",
                             "description", out, sizeof(out)));
    T_STR(out, "Bad Request");

    t_begin("whitespace around the colon is tolerated");
    T_OK(bxl_json_get_string("{ \"description\" : \"spaced\" }",
                             "description", out, sizeof(out)));
    T_STR(out, "spaced");

    t_begin("the JSON escapes are decoded");
    T_OK(bxl_json_get_string(
             "{\"description\":\"line\\nbreak \\\"quoted\\\" back\\\\slash\"}",
             "description", out, sizeof(out)));
    T_STR(out, "line\nbreak \"quoted\" back\\slash");

    t_begin("a \\u escape becomes UTF-8");
    T_OK(bxl_json_get_string("{\"description\":\"caf\\u00e9\"}",
                             "description", out, sizeof(out)));
    T_STR(out, "caf\xC3\xA9");

    t_begin("a surrogate pair becomes one code point");
    T_OK(bxl_json_get_string("{\"description\":\"\\uD83D\\uDE80\"}",
                             "description", out, sizeof(out)));
    T_STR(out, "\xF0\x9F\x9A\x80");   /* U+1F680 ROCKET */

    t_begin("a key that also appears as a value is not mistaken for the key");
    T_OK(bxl_json_get_string(
             "{\"text\":\"description\",\"description\":\"real\"}",
             "description", out, sizeof(out)));
    T_STR(out, "real");

    t_begin("a missing key reports failure and empties the buffer");
    T_OK(!bxl_json_get_string("{\"ok\":true}", "description",
                              out, sizeof(out)));
    T_STR(out, "");

    t_begin("a non-string value is rejected rather than coerced");
    T_OK(!bxl_json_get_string("{\"description\":42}", "description",
                              out, sizeof(out)));

    t_begin("a long description is truncated, not failed");
    {
        char big[2048];
        char tiny[16];
        size_t i;
        for (i = 0; i < sizeof(big) - 1; i++) big[i] = 'x';
        big[sizeof(big) - 1] = 0;
        {
            char json[2200];
            StringCchPrintfA(json, sizeof(json), "{\"description\":\"%s\"}", big);
            T_OK(bxl_json_get_string(json, "description", tiny, sizeof(tiny)));
            T_INT(strlen(tiny), 15);
        }
    }
}

static void t_json_bool_int(void)
{
    int b = -1;
    long v = 0;

    t_begin("a true boolean is read");
    T_OK(bxl_json_get_bool("{\"ok\":true,\"result\":{}}", "ok", &b));
    T_INT(b, 1);

    t_begin("a false boolean is read");
    T_OK(bxl_json_get_bool("{\"ok\":false}", "ok", &b));
    T_INT(b, 0);

    t_begin("a boolean key that is absent reports failure");
    T_OK(!bxl_json_get_bool("{\"other\":true}", "ok", &b));

    t_begin("a quoted boolean is not accepted");
    T_OK(!bxl_json_get_bool("{\"ok\":\"true\"}", "ok", &b));

    t_begin("an integer is read");
    T_OK(bxl_json_get_int("{\"error_code\":400}", "error_code", &v));
    T_INT(v, 400);

    t_begin("a negative integer is read");
    T_OK(bxl_json_get_int("{\"error_code\":-1}", "error_code", &v));
    T_INT(v, -1);

    t_begin("retry_after is read from an envelope");
    T_OK(bxl_json_get_int("{\"ok\":false,\"parameters\":{\"retry_after\":17}}",
                          "retry_after", &v));
    T_INT(v, 17);

    t_begin("a non-numeric value is rejected");
    T_OK(!bxl_json_get_int("{\"error_code\":\"400\"}", "error_code", &v));
}

/*----------------------------------------------------------------------------
 * multipart/form-data
 *--------------------------------------------------------------------------*/
static void t_multipart(void)
{
    static const unsigned char blob[] = { 0x00, 0x01, 0xFF, 0x7F, 0x0A, 0x0D };
    BxlMultipart *mp;
    const char   *body;
    size_t        len = 0;
    const char   *ctype;

    t_begin("a form can be started");
    mp = bxl_mp_begin();
    T_OK(mp != NULL);
    if (!mp) return;

    t_begin("the content type carries the boundary");
    ctype = bxl_mp_content_type(mp);
    T_OK(ctype != NULL);
    T_OK(strstr(ctype, "multipart/form-data; boundary=") != NULL);

    t_begin("a text field is framed as a form-data part");
    T_OK(bxl_mp_add_field(mp, "chat_id", "12345"));

    t_begin("a binary file part is accepted");
    T_OK(bxl_mp_add_file(mp, "photo", "shot.png", "image/png",
                         blob, sizeof(blob)));

    t_begin("the body is terminated by the closing delimiter");
    body = bxl_mp_data(mp, &len);
    T_OK(body != NULL);
    T_OK(len > 0);
    {
        /* RFC 2046: the terminator is "--<boundary>--\r\n". */
        const char *bnd = strstr(ctype, "boundary=");
        char        close[160];

        T_OK(bnd != NULL);
        bnd += 9;
        StringCchPrintfA(close, sizeof(close), "--%s--\r\n", bnd);
        T_OK(len >= strlen(close));
        T_OK(memcmp(body + len - strlen(close), close, strlen(close)) == 0);
    }

    t_begin("reading the body twice does not append a second delimiter");
    {
        size_t len2 = 0;
        const char *body2 = bxl_mp_data(mp, &len2);
        T_OK(body2 == body);
        T_INT(len2, len);
    }

    t_begin("the field part carries its name and value");
    {
        char needle[128];
        StringCchPrintfA(needle, sizeof(needle),
                         "Content-Disposition: form-data; name=\"chat_id\"");
        T_OK(find_bytes(body, len, needle, strlen(needle)) != NULL);
        T_OK(find_bytes(body, len, "12345", 5) != NULL);
    }

    t_begin("the file part carries its filename and media type");
    {
        const char *fn = "filename=\"shot.png\"";
        const char *mt = "Content-Type: image/png";
        T_OK(find_bytes(body, len, fn, strlen(fn)) != NULL);
        T_OK(find_bytes(body, len, mt, strlen(mt)) != NULL);
    }

    t_begin("the binary payload survives byte-for-byte");
    T_OK(find_bytes(body, len, blob, sizeof(blob)) != NULL);

    t_begin("every part begins with the same boundary");
    {
        char delim[160];
        StringCchPrintfA(delim, sizeof(delim), "--%s",
                         ctype + strlen("multipart/form-data; boundary="));
        /* two parts + closing delimiter = three occurrences */
        T_OK(find_bytes(body, len, delim, strlen(delim)) != NULL);
    }

    bxl_mp_free(mp);

    t_begin("freeing a NULL form is safe");
    bxl_mp_free(NULL);
}

static void t_multipart_edges(void)
{
    BxlMultipart *mp;

    t_begin("a NULL form is rejected by every accessor");
    T_OK(!bxl_mp_add_field(NULL, "a", "b"));
    T_OK(!bxl_mp_add_file(NULL, "a", "b", "c", "d", 1));
    T_OK(bxl_mp_data(NULL, NULL) == NULL);
    T_OK(bxl_mp_content_type(NULL) == NULL);

    t_begin("adding a field with NULL arguments is rejected");
    mp = bxl_mp_begin();
    T_OK(mp != NULL);
    if (mp) {
        T_OK(!bxl_mp_add_field(mp, NULL, "v"));
        T_OK(!bxl_mp_add_field(mp, "n", NULL));
        T_OK(!bxl_mp_add_file(mp, "n", NULL, "m", "d", 1));
        bxl_mp_free(mp);
    }
}

static void t_response_lifecycle(void)
{
    BxlHttpResponse res;

    t_begin("freeing a zeroed response is safe");
    memset(&res, 0, sizeof(res));
    bxl_http_response_free(&res);
    T_OK(res.body == NULL);

    t_begin("freeing a NULL response is safe");
    bxl_http_response_free(NULL);
}

void test_http(void)
{
    t_suite("http / json / multipart");
    t_json_strings();
    t_json_bool_int();
    t_multipart();
    t_multipart_edges();
    t_response_lifecycle();
}
