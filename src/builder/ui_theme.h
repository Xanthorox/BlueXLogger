/*============================================================================
 * BlueXLogger - src/builder/ui_theme.h
 * Dark theme palette, fonts and drawing primitives for BlueXBuilder.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * The builder deliberately avoids the default MFC-gray look. Everything the
 * operator sees is either owner-drawn by the small widget classes in
 * ui_widget.c or painted by the main window from the palette below.
 *==========================================================================*/
#ifndef BXL_UI_THEME_H
#define BXL_UI_THEME_H

#include "bxl_common.h"

/*----------------------------------------------------------------------------
 * Palette. Values are 0x00BBGGRR as COLORREF expects.
 *--------------------------------------------------------------------------*/
typedef struct BxlPalette {
    COLORREF bg;            /* window background                        */
    COLORREF surface;       /* panels, nav rail                         */
    COLORREF surface_hi;    /* input wells, log panel                   */
    COLORREF surface_hi2;   /* hovered input wells                      */
    COLORREF border;        /* hairline separators                      */
    COLORREF border_soft;   /* input outlines at rest                   */
    COLORREF text;          /* primary text                             */
    COLORREF text_dim;      /* labels, hints                            */
    COLORREF text_faint;    /* watermark, disabled                      */
    COLORREF accent;        /* primary blue                             */
    COLORREF accent_hi;     /* hovered blue                             */
    COLORREF accent_lo;     /* pressed blue                             */
    COLORREF accent_text;   /* text on accent                           */
    COLORREF ok;
    COLORREF warn;
    COLORREF err;
    COLORREF header;        /* title bar strip                          */
} BxlPalette;

typedef struct BxlFonts {
    HFONT title;            /* product name in the header               */
    HFONT h1;               /* page heading                             */
    HFONT h2;               /* section heading                          */
    HFONT body;             /* labels and input text                    */
    HFONT hint;             /* hints, footer - NOT named "small": the    */
                            /* Windows headers still #define small char */
    HFONT mono;             /* build log                                */
} BxlFonts;

/* Global theme state, initialised once by theme_init(). */
extern BxlPalette g_pal;
extern BxlFonts   g_font;

/* Scale factor for the window's DPI (1.0 at 96 DPI). */
extern int g_dpi;

/* Convert a design-unit (96 DPI) value to device pixels. */
int ui_scale(int design_units);

/* Create fonts, brushes and pens. Safe to call again on WM_DPICHANGED. */
void theme_init(HWND hwnd);
void theme_free(void);

/* Ask the theme engine for a scrollbar that belongs on a dark surface. A
 * no-op on builds that do not know the dark theme, where the stock scrollbar
 * is drawn instead. */
void ui_dark_scrollbars(HWND h);

/*----------------------------------------------------------------------------
 * Drawing primitives
 *--------------------------------------------------------------------------*/

/* Fill a rectangle with a solid colour. */
void ui_fill(HDC dc, const RECT *r, COLORREF c);

/* Fill a rounded rectangle. radius is in device pixels. */
void ui_fill_round(HDC dc, const RECT *r, int radius, COLORREF c);

/* Draw a rounded rectangle outline of the given width. */
void ui_frame_round(HDC dc, const RECT *r, int radius, COLORREF c, int width);

/* Vertical gradient fill, used for the header strip and the progress bar. */
void ui_gradient_v(HDC dc, const RECT *r, COLORREF top, COLORREF bottom);

/* The colour ui_gradient_v() would put on row y of an h-tall gradient. Lets a
 * child control sitting on a gradient pick a solid backdrop that blends in. */
COLORREF ui_gradient_v_at(int y, int h, COLORREF top, COLORREF bottom);

/* Draw a 1px horizontal rule. */
void ui_hline(HDC dc, int x1, int x2, int y, COLORREF c);

/* Draw text clipped to r. flags are DrawTextW flags; DT_NOPREFIX is added
 * automatically so that ampersands in labels render literally. */
void ui_text(HDC dc, const RECT *r, const wchar_t *s, HFONT f,
             COLORREF c, UINT flags);

/* Measure the width of s in font f, in device pixels. */
int ui_text_width(HDC dc, const wchar_t *s, HFONT f);

/* Colour helpers. */
COLORREF ui_mix(COLORREF a, COLORREF b, int percent_of_b);
COLORREF ui_lighten(COLORREF c, int amount);
COLORREF ui_darken(COLORREF c, int amount);

/* Rounded rectangle path helpers built on a cached pen/brush pair so the
 * primitives above do not allocate GDI objects per call. */
HPEN   ui_pen(COLORREF c, int width);
HBRUSH ui_brush(COLORREF c);

#endif /* BXL_UI_THEME_H */
