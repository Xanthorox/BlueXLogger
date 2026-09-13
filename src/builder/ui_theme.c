/*============================================================================
 * BlueXLogger - src/builder/ui_theme.c
 * Dark theme palette, fonts and drawing primitives.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "ui_theme.h"
#include <uxtheme.h>

BxlPalette g_pal;
BxlFonts   g_font;
int        g_dpi = 96;

/* One-entry GDI object cache. A single paint pass normally draws many shapes
 * in the same colour, so this removes almost all of the churn without the
 * bookkeeping (and staleness bugs) of a real hash cache. */
static HPEN     s_pen;
static COLORREF s_pen_c;
static int      s_pen_w;
static HBRUSH   s_brush;
static COLORREF s_brush_c;

int ui_scale(int design_units)
{
    return MulDiv(design_units, g_dpi, 96);
}

COLORREF ui_mix(COLORREF a, COLORREF b, int pct_of_b)
{
    int r, g, bl;
    if (pct_of_b < 0)   pct_of_b = 0;
    if (pct_of_b > 100) pct_of_b = 100;
    r  = GetRValue(a) + (GetRValue(b) - GetRValue(a)) * pct_of_b / 100;
    g  = GetGValue(a) + (GetGValue(b) - GetGValue(a)) * pct_of_b / 100;
    bl = GetBValue(a) + (GetBValue(b) - GetBValue(a)) * pct_of_b / 100;
    return RGB(r, g, bl);
}

COLORREF ui_lighten(COLORREF c, int amount)
{
    return ui_mix(c, RGB(255, 255, 255), amount);
}

COLORREF ui_darken(COLORREF c, int amount)
{
    return ui_mix(c, RGB(0, 0, 0), amount);
}

HPEN ui_pen(COLORREF c, int width)
{
    if (s_pen && s_pen_c == c && s_pen_w == width) return s_pen;
    if (s_pen) { DeleteObject(s_pen); s_pen = NULL; }
    s_pen   = CreatePen(PS_SOLID, width, c);
    s_pen_c = c;
    s_pen_w = width;
    return s_pen;
}

HBRUSH ui_brush(COLORREF c)
{
    if (s_brush && s_brush_c == c) return s_brush;
    if (s_brush) { DeleteObject(s_brush); s_brush = NULL; }
    s_brush   = CreateSolidBrush(c);
    s_brush_c = c;
    return s_brush;
}

static HFONT make_font(const wchar_t *face, int pt, int weight)
{
    LOGFONTW lf;
    HDC      dc = GetDC(NULL);
    int      height;

    memset(&lf, 0, sizeof(lf));
    height = -MulDiv(pt, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(NULL, dc);

    lf.lfHeight         = height;
    lf.lfWeight         = weight;
    lf.lfCharSet        = DEFAULT_CHARSET;
    lf.lfQuality        = CLEARTYPE_QUALITY;
    lf.lfOutPrecision   = OUT_TT_PRECIS;
    lf.lfClipPrecision  = CLIP_DEFAULT_PRECIS;
    StringCchCopyW(lf.lfFaceName, LF_FACESIZE, face);
    return CreateFontIndirectW(&lf);
}

void theme_init(HWND hwnd)
{
    HDC dc;

    if (hwnd) {
        g_dpi = (int)GetDpiForWindow(hwnd);
        if (g_dpi < 96) g_dpi = 96;
    }

    /* ---- palette -------------------------------------------------------- */
    g_pal.bg           = RGB(0x12, 0x14, 0x1A);
    g_pal.surface      = RGB(0x18, 0x1B, 0x23);
    g_pal.surface_hi   = RGB(0x22, 0x26, 0x30);
    g_pal.surface_hi2  = RGB(0x2A, 0x2F, 0x3B);
    g_pal.border       = RGB(0x2A, 0x2E, 0x39);
    g_pal.border_soft  = RGB(0x33, 0x39, 0x47);
    g_pal.text         = RGB(0xE8, 0xEB, 0xF2);
    g_pal.text_dim     = RGB(0x9A, 0xA3, 0xB5);
    g_pal.text_faint   = RGB(0x64, 0x6C, 0x7C);
    g_pal.accent       = RGB(0x3B, 0x82, 0xF6);
    g_pal.accent_hi    = RGB(0x5B, 0x9B, 0xFF);
    g_pal.accent_lo    = RGB(0x25, 0x63, 0xEB);
    g_pal.accent_text  = RGB(0xFF, 0xFF, 0xFF);
    g_pal.ok           = RGB(0x22, 0xC5, 0x5E);
    g_pal.warn         = RGB(0xF5, 0x9E, 0x0B);
    g_pal.err          = RGB(0xEF, 0x44, 0x44);
    g_pal.header       = RGB(0x0E, 0x10, 0x15);

    /* ---- fonts ---------------------------------------------------------- */
    dc = GetDC(NULL);
    g_font.title = make_font(L"Segoe UI", 20, FW_SEMIBOLD);
    g_font.h1    = make_font(L"Segoe UI", 15, FW_SEMIBOLD);
    g_font.h2    = make_font(L"Segoe UI", 11, FW_SEMIBOLD);
    g_font.body  = make_font(L"Segoe UI", 10, FW_NORMAL);
    g_font.hint  = make_font(L"Segoe UI",  9, FW_NORMAL);
    g_font.mono  = make_font(L"Consolas",  9, FW_NORMAL);
    ReleaseDC(NULL, dc);
}

void ui_dark_scrollbars(HWND h)
{
    /* uxtheme ships with the Windows SDK, so this costs no third-party
     * dependency. "DarkMode_Explorer" is the theme name Windows 11 uses for
     * its own dark scrollbars; earlier builds simply do not have it and the
     * call fails harmlessly, leaving the stock scrollbar in place. */
    SetWindowTheme(h, L"DarkMode_Explorer", NULL);
}

void theme_free(void)
{
    if (s_pen)   { DeleteObject(s_pen);   s_pen   = NULL; }
    if (s_brush) { DeleteObject(s_brush); s_brush = NULL; }

    if (g_font.title) DeleteObject(g_font.title);
    if (g_font.h1)    DeleteObject(g_font.h1);
    if (g_font.h2)    DeleteObject(g_font.h2);
    if (g_font.body)  DeleteObject(g_font.body);
    if (g_font.hint)  DeleteObject(g_font.hint);
    if (g_font.mono)  DeleteObject(g_font.mono);
    memset(&g_font, 0, sizeof(g_font));
}

void ui_fill(HDC dc, const RECT *r, COLORREF c)
{
    HBRUSH b = ui_brush(c);
    FillRect(dc, r, b);
}

void ui_fill_round(HDC dc, const RECT *r, int radius, COLORREF c)
{
    HGDIOBJ ob = SelectObject(dc, ui_brush(c));
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, r->left, r->top, r->right, r->bottom, radius * 2, radius * 2);
    SelectObject(dc, op);
    SelectObject(dc, ob);
}

void ui_frame_round(HDC dc, const RECT *r, int radius, COLORREF c, int width)
{
    HGDIOBJ op = SelectObject(dc, ui_pen(c, width));
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, r->left, r->top, r->right, r->bottom, radius * 2, radius * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
}

COLORREF ui_gradient_v_at(int y, int h, COLORREF top, COLORREF bottom)
{
    if (h <= 1) return top;
    if (y < 0) y = 0;
    if (y >= h) y = h - 1;
    return ui_mix(top, bottom, (y * 100) / (h - 1));
}

void ui_gradient_v(HDC dc, const RECT *r, COLORREF top, COLORREF bottom)
{
    int h = r->bottom - r->top;
    int y;

    if (h <= 0) return;
    for (y = 0; y < h; y++) {
        RECT line = *r;
        COLORREF c = ui_gradient_v_at(y, h, top, bottom);
        line.top = r->top + y;
        line.bottom = line.top + 1;
        ui_fill(dc, &line, c);
    }
}

void ui_hline(HDC dc, int x1, int x2, int y, COLORREF c)
{
    RECT r;
    r.left = x1; r.right = x2; r.top = y; r.bottom = y + 1;
    ui_fill(dc, &r, c);
}

void ui_text(HDC dc, const RECT *r, const wchar_t *s, HFONT f,
             COLORREF c, UINT flags)
{
    HGDIOBJ old;
    if (!s) return;
    old = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, (RECT *)r, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}

int ui_text_width(HDC dc, const wchar_t *s, HFONT f)
{
    HGDIOBJ old;
    SIZE    sz;
    if (!s) return 0;
    old = SelectObject(dc, f);
    sz.cx = 0; sz.cy = 0;
    GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
    SelectObject(dc, old);
    return sz.cx;
}
