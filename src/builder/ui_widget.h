/*============================================================================
 * BlueXLogger - src/builder/ui_widget.h
 * Small owner-drawn widget set for BlueXBuilder.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Three custom window classes are provided:
 *
 *   BxlButton  push button, four visual styles
 *   BxlCheck   checkbox with a modern square tick box
 *   BxlSeg     segmented control for small enumerations
 *
 * All three post a plain WM_COMMAND with HIWORD(wParam) == 0 and
 * lParam == the control HWND when the user activates them, so the parent's
 * command handling looks exactly like it would for a stock control.
 *
 * Text fields use the stock EDIT control, themed by the parent's
 * WM_CTLCOLOR* handlers; the small frames around them are painted by the
 * parent so that they match the palette exactly.
 *==========================================================================*/
#ifndef BXL_UI_WIDGET_H
#define BXL_UI_WIDGET_H

#include "bxl_common.h"

/* Button visual styles. */
#define UI_BTN_PRIMARY    0
#define UI_BTN_SECONDARY  1
#define UI_BTN_GHOST      2
#define UI_BTN_DANGER     3

/* Register the three window classes. Call once, before creating any window. */
int ui_register_classes(HINSTANCE inst);

/* Creation. ui_make_seg takes a pipe-separated label list, e.g. L"Off|Run key".
 * Returns NULL on failure. */
HWND ui_make_button(HWND parent, int id, const wchar_t *text, int style);
HWND ui_make_check(HWND parent, int id, const wchar_t *text, int checked);
HWND ui_make_seg(HWND parent, int id, const wchar_t *labels, int sel);

/* State accessors. */
int  ui_get_check(HWND h);
void ui_set_check(HWND h, int checked);
int  ui_seg_get_sel(HWND h);
void ui_seg_set_sel(HWND h, int sel);
void ui_set_text(HWND h, const wchar_t *text);

/* Tell a widget the colour of the surface it is sitting on. The widget fills
 * its backing buffer with this colour, so any part it deliberately leaves
 * unpainted blends into the parent. Defaults to the window background; set it
 * for widgets placed on the header gradient or the footer panel. */
void ui_set_bg(HWND h, COLORREF bg);

/* Mark a control as failing validation. The parent paints the red outline
 * itself from its control table; this only tells the widget so that it can
 * adjust its own text colour. */
void ui_set_invalid(HWND h, int invalid);
int  ui_get_invalid(HWND h);

/* Convenience for the parent's WM_CTLCOLOR handlers. */
HBRUSH ui_edit_brush(void);

#endif /* BXL_UI_WIDGET_H */
