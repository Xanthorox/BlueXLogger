/*============================================================================
 * BlueXLogger - src/builder/ui_widget.c
 * Owner-drawn button, checkbox and segmented control.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "ui_widget.h"
#include "ui_theme.h"

#define WK_BUTTON 1
#define WK_CHECK  2
#define WK_SEG    3

#define SEG_MAX   8
#define SEG_CCH   48

typedef struct BxlWnd {
    int   kind;
    int   style;        /* WK_BUTTON */
    int   checked;      /* WK_CHECK  */
    int   invalid;
    int   hover;        /* bool for button/check */
    int   hover_seg;    /* index or -1 for WK_SEG */
    int   pressed;
    int   focused;
    int   seg_count;
    int   seg_sel;
    COLORREF bg;        /* colour of the parent surface behind this widget */
    wchar_t seg_labels[SEG_MAX][SEG_CCH];
} BxlWnd;

typedef struct BxlWndCreate {
    int   kind;         /* WK_BUTTON / WK_CHECK / WK_SEG */
    int   style;
    int   checked;
    int   seg_sel;
    const wchar_t *seg_labels;
} BxlWndCreate;

static const wchar_t *k_button_class = L"BxlButton";
static const wchar_t *k_check_class  = L"BxlCheck";
static const wchar_t *k_seg_class    = L"BxlSeg";

static HBRUSH g_edit_brush;

HBRUSH ui_edit_brush(void)
{
    if (!g_edit_brush) g_edit_brush = CreateSolidBrush(g_pal.surface_hi);
    return g_edit_brush;
}

static void notify_parent(HWND h, int id)
{
    HWND p = GetParent(h);
    if (p) SendMessageW(p, WM_COMMAND, MAKEWPARAM(id, 0), (LPARAM)h);
}

static void parse_segments(BxlWnd *w, const wchar_t *labels)
{
    const wchar_t *p = labels;
    int n = 0;

    w->seg_count = 0;
    if (!labels) return;

    while (*p && n < SEG_MAX) {
        int i = 0;
        while (*p && *p != L'|' && i < SEG_CCH - 1) w->seg_labels[n][i++] = *p++;
        w->seg_labels[n][i] = 0;
        n++;
        if (*p == L'|') p++;
    }
    w->seg_count = n;
    if (w->seg_sel >= n) w->seg_sel = n > 0 ? 0 : -1;
}

/*----------------------------------------------------------------------------
 * Painting
 *--------------------------------------------------------------------------*/
static void paint_button(HDC dc, BxlWnd *w, const RECT *r, const wchar_t *cap,
                         int enabled)
{
    COLORREF fill = g_pal.surface_hi;
    COLORREF edge = g_pal.border_soft;
    COLORREF txt  = g_pal.text;
    int      radius = ui_scale(6);
    int      draw_fill = 1, draw_edge = 1;

    switch (w->style) {
    case UI_BTN_PRIMARY:
        fill = w->pressed ? g_pal.accent_lo
                          : (w->hover ? g_pal.accent_hi : g_pal.accent);
        edge = fill;
        txt  = g_pal.accent_text;
        break;
    case UI_BTN_DANGER:
        fill = w->pressed ? ui_darken(g_pal.err, 18)
                          : (w->hover ? ui_lighten(g_pal.err, 12) : g_pal.err);
        edge = fill;
        txt  = g_pal.accent_text;
        break;
    case UI_BTN_GHOST:
        draw_fill = w->hover || w->pressed;
        draw_edge = 0;
        fill = w->pressed ? g_pal.surface_hi2 : g_pal.surface_hi;
        txt  = w->hover ? g_pal.text : g_pal.text_dim;
        break;
    default: /* UI_BTN_SECONDARY */
        fill = w->pressed ? ui_darken(g_pal.surface_hi2, 10)
                          : (w->hover ? g_pal.surface_hi2 : g_pal.surface_hi);
        edge = w->hover ? g_pal.accent : g_pal.border_soft;
        break;
    }

    if (!enabled) {
        fill = g_pal.surface_hi;
        edge = g_pal.border;
        txt  = g_pal.text_faint;
        draw_fill = 1;
        draw_edge = 1;
    }

    if (draw_fill) ui_fill_round(dc, r, radius, fill);
    if (draw_edge) ui_frame_round(dc, r, radius, edge, 1);

    if (enabled && w->focused &&
        w->style != UI_BTN_PRIMARY && w->style != UI_BTN_DANGER) {
        RECT f = *r;
        InflateRect(&f, -ui_scale(3), -ui_scale(3));
        ui_frame_round(dc, &f, radius, ui_mix(g_pal.accent, fill, 45), 1);
    }
    if (w->invalid) ui_frame_round(dc, r, radius, g_pal.err, 2);

    ui_text(dc, r, cap, g_font.body, txt,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void paint_check(HDC dc, BxlWnd *w, const RECT *r, const wchar_t *label,
                        int enabled)
{
    int      box = ui_scale(17);
    int      radius = ui_scale(4);
    RECT     b;
    RECT     t;

    b.left   = r->left + ui_scale(2);
    b.top    = r->top + (r->bottom - r->top - box) / 2;
    b.right  = b.left + box;
    b.bottom = b.top + box;

    ui_fill_round(dc, &b, radius,
                  w->checked ? (enabled ? g_pal.accent : g_pal.text_faint)
                             : g_pal.surface_hi2);
    ui_frame_round(dc, &b, radius,
                   w->checked ? (enabled ? g_pal.accent : g_pal.text_faint)
                              : (w->hover ? g_pal.accent : g_pal.border_soft), 1);

    if (w->checked) {
        HPEN  pen = ui_pen(g_pal.accent_text, ui_scale(2));
        HGDIOBJ op = SelectObject(dc, pen);
        int x0 = b.left + ui_scale(4), y0 = b.top + ui_scale(8);
        int x1 = b.left + ui_scale(7), y1 = b.top + ui_scale(11);
        int x2 = b.left + ui_scale(13), y2 = b.top + ui_scale(4);
        MoveToEx(dc, x0, y0, NULL); LineTo(dc, x1, y1); LineTo(dc, x2, y2);
        SelectObject(dc, op);
    }

    t = *r;
    t.left = b.right + ui_scale(9);
    ui_text(dc, &t, label, g_font.body,
            !enabled ? g_pal.text_faint
                     : (w->hover || w->checked ? g_pal.text : g_pal.text_dim),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (enabled && w->focused) {
        RECT f = *r;
        InflateRect(&f, -ui_scale(2), -ui_scale(2));
        ui_frame_round(dc, &f, ui_scale(4), ui_mix(g_pal.accent, g_pal.bg, 40), 1);
    }
    if (w->invalid) ui_frame_round(dc, r, radius, g_pal.err, 2);
}

static void paint_seg(HDC dc, BxlWnd *w, const RECT *r)
{
    int radius = ui_scale(6);
    int gap    = ui_scale(2);
    int total  = r->right - r->left - gap * 2;
    int i;

    if (w->seg_count <= 0) return;

    ui_fill_round(dc, r, radius, g_pal.surface_hi);
    ui_frame_round(dc, r, radius, g_pal.border_soft, 1);

    for (i = 0; i < w->seg_count; i++) {
        RECT s;
        int  x0 = r->left + gap + (int)((long long)total * i / w->seg_count);
        int  x1 = r->left + gap + (int)((long long)total * (i + 1) / w->seg_count);
        int  selected = (i == w->seg_sel);

        s.left = x0; s.right = x1;
        s.top = r->top + gap; s.bottom = r->bottom - gap;

        if (selected) {
            ui_fill_round(dc, &s, radius - gap, g_pal.accent);
        } else if (i == w->hover_seg) {
            ui_fill_round(dc, &s, radius - gap, g_pal.surface_hi2);
        }

        ui_text(dc, &s, w->seg_labels[i], g_font.hint,
                selected ? g_pal.accent_text
                         : (i == w->hover_seg ? g_pal.text : g_pal.text_dim),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    if (w->focused) {
        RECT f = *r;
        InflateRect(&f, -ui_scale(3), -ui_scale(3));
        ui_frame_round(dc, &f, radius, ui_mix(g_pal.accent, g_pal.bg, 40), 1);
    }
    if (w->invalid) ui_frame_round(dc, r, radius, g_pal.err, 2);
}

static void paint_widget(HWND h, BxlWnd *w)
{
    PAINTSTRUCT ps;
    HDC     dc = BeginPaint(h, &ps);
    RECT    r;
    wchar_t cap[256];
    int     enabled;

    GetClientRect(h, &r);
    cap[0] = 0;
    GetWindowTextW(h, cap, (int)BXL_COUNT_OF(cap));
    enabled = IsWindowEnabled(h) ? 1 : 0;

    /* Double-buffer: the shapes are drawn with RoundRect and a gradient-free
     * fill, but the labels would still flicker against the parent's
     * background on hover repaints. */
    {
        HDC     mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, r.right, r.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);

        /* Seed the buffer with the colour of the surface this widget sits on.
         * A fresh compatible bitmap is zeroed, so the areas a widget
         * deliberately leaves unpainted - the body of a ghost button, the gaps
         * in a segmented control - would otherwise show up as black holes.
         *
         * Reading the pixels back from the parent is not an option: the window
         * has WS_CLIPCHILDREN, so the parent's DC has exactly the child regions
         * masked out and reading there yields garbage. The backdrop colour is
         * pushed in by the parent instead (ui_set_bg). */
        ui_fill(mem, &r, w->bg);

        switch (w->kind) {
        case WK_BUTTON: paint_button(mem, w, &r, cap, enabled); break;
        case WK_CHECK:  paint_check(mem, w, &r, cap, enabled);  break;
        default:        paint_seg(mem, w, &r);                  break;
        }

        BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
    }

    EndPaint(h, &ps);
}

/*----------------------------------------------------------------------------
 * Interaction
 *--------------------------------------------------------------------------*/
static void seg_hit(BxlWnd *w, const RECT *r, int x)
{
    int gap   = ui_scale(2);
    int total = r->right - r->left - gap * 2;
    int i;

    if (w->seg_count <= 0 || total <= 0) return;
    for (i = 0; i < w->seg_count; i++) {
        int x0 = r->left + gap + (int)((long long)total * i / w->seg_count);
        int x1 = r->left + gap + (int)((long long)total * (i + 1) / w->seg_count);
        if (x >= x0 && x < x1) { w->seg_sel = i; return; }
    }
}

static LRESULT CALLBACK widget_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    BxlWnd *w = (BxlWnd *)GetWindowLongPtrW(h, GWLP_USERDATA);

    if (msg == WM_NCCREATE) {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        BxlWndCreate  *cc = (BxlWndCreate *)cs->lpCreateParams;
        BxlWnd *nw = (BxlWnd *)calloc(1, sizeof(BxlWnd));

        if (!nw) return FALSE;
        if (cc) {
            nw->kind    = cc->kind;
            nw->style   = cc->style;
            nw->checked = cc->checked;
            nw->seg_sel = cc->seg_sel;
            parse_segments(nw, cc->seg_labels);
        }
        nw->hover_seg = -1;
        nw->bg        = g_pal.bg;
        /* Fallback for a caller that forgot to fill in BxlWndCreate.kind. The
         * class name arriving in WM_NCCREATE is a pointer into the window's
         * internal class-name storage, NOT the literal handed to
         * RegisterClassExW, so this has to be a string comparison - comparing
         * the pointers silently sends every widget down the WK_SEG path. */
        if (nw->kind != WK_BUTTON && nw->kind != WK_CHECK && nw->kind != WK_SEG) {
            if (wcscmp(cs->lpszClass, k_button_class) == 0)     nw->kind = WK_BUTTON;
            else if (wcscmp(cs->lpszClass, k_check_class) == 0) nw->kind = WK_CHECK;
            else                                                nw->kind = WK_SEG;
        }

        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)nw);

        /* The creation-time caption is not stored anywhere until
         * DefWindowProcW handles WM_NCCREATE - it is the default procedure
         * that writes the text passed to CreateWindowEx into the window's
         * internal text slot. Returning TRUE without chaining left every
         * button created with a caption showing an empty one, while widgets
         * retitled later through WM_SETTEXT worked fine. */
        return DefWindowProcW(h, msg, wp, lp);
    }

    if (!w) return DefWindowProcW(h, msg, wp, lp);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_SETTEXT: {
        /* The caption is drawn from GetWindowText at paint time. Storing it is
         * not enough: DefWindowProc does not reliably invalidate a custom
         * window class, so a widget whose label is set after creation (every
         * checkbox, and any button retitled by a profile load) would keep
         * showing the empty caption it was created with. */
        LRESULT r = DefWindowProcW(h, msg, wp, lp);
        InvalidateRect(h, NULL, FALSE);
        return r;
    }

    case WM_PAINT:
        paint_widget(h, w);
        return 0;

    case WM_MOUSEMOVE: {
        RECT r;
        GetClientRect(h, &r);
        if (!w->hover) {
            TRACKMOUSEEVENT tme;
            w->hover = 1;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = h;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
        }
        /* Hovering a segment must never change the selection - that only
         * happens on WM_LBUTTONDOWN - so this is a read-only hit test. */
        if (w->kind == WK_SEG) {
            int x     = GET_X_LPARAM(lp);
            int gap   = ui_scale(2);
            int total = r.right - r.left - gap * 2;
            int i;
            w->hover_seg = -1;
            for (i = 0; i < w->seg_count && total > 0; i++) {
                int x0 = r.left + gap + (int)((long long)total * i / w->seg_count);
                int x1 = r.left + gap + (int)((long long)total * (i + 1) / w->seg_count);
                if (x >= x0 && x < x1) { w->hover_seg = i; break; }
            }
        }
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }

    case WM_MOUSELEAVE:
        w->hover = 0;
        w->hover_seg = -1;
        w->pressed = 0;
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        if (w->kind == WK_SEG) {
            RECT r;
            GetClientRect(h, &r);
            seg_hit(w, &r, GET_X_LPARAM(lp));
            notify_parent(h, GetDlgCtrlID(h));
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        w->pressed = 1;
        SetCapture(h);
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_LBUTTONUP:
        if (w->kind != WK_SEG) {
            int inside;
            RECT r;
            POINT pt;
            GetClientRect(h, &r);
            pt.x = GET_X_LPARAM(lp);
            pt.y = GET_Y_LPARAM(lp);
            inside = PtInRect(&r, pt);
            if (w->pressed && inside) {
                if (w->kind == WK_CHECK) w->checked = !w->checked;
                notify_parent(h, GetDlgCtrlID(h));
            }
            w->pressed = 0;
            if (GetCapture() == h) ReleaseCapture();
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_SPACE || wp == VK_RETURN) {
            if (w->kind == WK_CHECK) w->checked = !w->checked;
            if (w->kind == WK_SEG) {
                if (wp == VK_RIGHT && w->seg_sel + 1 < w->seg_count) w->seg_sel++;
                if (wp == VK_LEFT  && w->seg_sel > 0) w->seg_sel--;
            }
            notify_parent(h, GetDlgCtrlID(h));
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        break;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        w->focused = (msg == WM_SETFOCUS);
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;

    case WM_NCDESTROY:
        free(w);
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        break;
    }

    return DefWindowProcW(h, msg, wp, lp);
}

/*----------------------------------------------------------------------------
 * Class registration and creation
 *--------------------------------------------------------------------------*/
static int register_one(HINSTANCE inst, const wchar_t *name)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = widget_proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = name;
    return RegisterClassExW(&wc) != 0;
}

int ui_register_classes(HINSTANCE inst)
{
    if (!register_one(inst, k_button_class)) return 0;
    if (!register_one(inst, k_check_class))  return 0;
    if (!register_one(inst, k_seg_class))    return 0;
    return 1;
}

static HWND make(HWND parent, const wchar_t *cls, int id, const wchar_t *text,
                 BxlWndCreate *cc, DWORD style)
{
    return CreateWindowExW(0, cls, text ? text : L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                           0, 0, 10, 10, parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(NULL), cc);
}

HWND ui_make_button(HWND parent, int id, const wchar_t *text, int style)
{
    BxlWndCreate cc;
    cc.kind = WK_BUTTON;
    cc.style = style; cc.checked = 0; cc.seg_sel = 0; cc.seg_labels = NULL;
    return make(parent, k_button_class, id, text, &cc, BS_OWNERDRAW);
}

HWND ui_make_check(HWND parent, int id, const wchar_t *text, int checked)
{
    BxlWndCreate cc;
    cc.kind = WK_CHECK;
    cc.style = 0; cc.checked = checked; cc.seg_sel = 0; cc.seg_labels = NULL;
    return make(parent, k_check_class, id, text, &cc, 0);
}

HWND ui_make_seg(HWND parent, int id, const wchar_t *labels, int sel)
{
    BxlWndCreate cc;
    cc.kind = WK_SEG;
    cc.style = 0; cc.checked = 0; cc.seg_sel = sel; cc.seg_labels = labels;
    return make(parent, k_seg_class, id, L"", &cc, 0);
}

int ui_get_check(HWND h)
{
    BxlWnd *w = (BxlWnd *)GetWindowLongPtrW(h, GWLP_USERDATA);
    return w ? w->checked : 0;
}

void ui_set_check(HWND h, int checked)
{
    BxlWnd *w = (BxlWnd *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (w) { w->checked = checked ? 1 : 0; InvalidateRect(h, NULL, FALSE); }
}

int ui_seg_get_sel(HWND h)
{
    BxlWnd *w = (BxlWnd *)GetWindowLongPtrW(h, GWLP_USERDATA);
    return w ? w->seg_sel : 0;
}

void ui_seg_set_sel(HWND h, int sel)
{
    BxlWnd *w = (BxlWnd *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (w) { w->seg_sel = sel; InvalidateRect(h, NULL, FALSE); }
}

void ui_set_text(HWND h, const wchar_t *text)
{
    SetWindowTextW(h, text ? text : L"");
}

void ui_set_bg(HWND h, COLORREF bg)
{
    BxlWnd *w = (BxlWnd *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (w && w->bg != bg) { w->bg = bg; InvalidateRect(h, NULL, FALSE); }
}

void ui_set_invalid(HWND h, int invalid)
{
    BxlWnd *w = (BxlWnd *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (w) { w->invalid = invalid ? 1 : 0; InvalidateRect(h, NULL, FALSE); }
}

int ui_get_invalid(HWND h)
{
    BxlWnd *w = (BxlWnd *)GetWindowLongPtrW(h, GWLP_USERDATA);
    return w ? w->invalid : 0;
}
