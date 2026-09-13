/*============================================================================
 * BlueXLogger - bxl_util.c
 * Growable buffers, UTF-8 bridging, time, path and file helpers.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "bxl_util.h"

#include <stdarg.h>

/*==========================================================================
 * BxlBuf
 *========================================================================*/
int bxl_buf_init(BxlBuf *b, size_t initial_cap)
{
    if (!b) return BXL_FALSE;
    if (initial_cap < 64) initial_cap = 64;

    b->data = (char *)malloc(initial_cap);
    if (!b->data) {
        b->len = b->cap = 0;
        return BXL_FALSE;
    }
    b->cap = initial_cap;
    b->len = 0;
    b->data[0] = '\0';
    return BXL_TRUE;
}

void bxl_buf_free(BxlBuf *b)
{
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

void bxl_buf_reset(BxlBuf *b)
{
    if (!b) return;
    b->len = 0;
    if (b->data && b->cap) b->data[0] = '\0';
}

int bxl_buf_reserve(BxlBuf *b, size_t extra)
{
    size_t need, cap;
    char  *np;

    if (!b) return BXL_FALSE;
    need = b->len + extra + 1;
    if (need <= b->cap) return BXL_TRUE;

    cap = b->cap ? b->cap : 64;
    while (cap < need) {
        if (cap > (SIZE_MAX / 2)) return BXL_FALSE;
        cap *= 2;
    }

    np = (char *)realloc(b->data, cap);
    if (!np) return BXL_FALSE;

    b->data = np;
    b->cap  = cap;
    return BXL_TRUE;
}

int bxl_buf_append(BxlBuf *b, const void *data, size_t len)
{
    if (!b || !data) return BXL_FALSE;
    if (len == 0) return BXL_TRUE;
    if (!bxl_buf_reserve(b, len)) return BXL_FALSE;

    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = '\0';
    return BXL_TRUE;
}

int bxl_buf_appends(BxlBuf *b, const char *s)
{
    if (!s) return BXL_TRUE;
    return bxl_buf_append(b, s, strlen(s));
}

int bxl_buf_appendc(BxlBuf *b, char c)
{
    return bxl_buf_append(b, &c, 1);
}

int bxl_buf_appendf(BxlBuf *b, const char *fmt, ...)
{
    va_list ap;
    int     n;
    size_t  avail;

    if (!b || !fmt) return BXL_FALSE;
    if (!bxl_buf_reserve(b, 256)) return BXL_FALSE;

    for (;;) {
        avail = b->cap - b->len;
        va_start(ap, fmt);
        n = _vsnprintf_s(b->data + b->len, avail, _TRUNCATE, fmt, ap);
        va_end(ap);

        if (n >= 0) {
            b->len += (size_t)n;
            return BXL_TRUE;
        }
        /* Truncated - grow and retry. */
        if (!bxl_buf_reserve(b, b->cap)) return BXL_FALSE;
    }
}

int bxl_buf_append_utf16n(BxlBuf *b, const wchar_t *w, size_t wlen)
{
    int need;
    if (!b || !w) return BXL_TRUE;
    if (wlen == 0) return BXL_TRUE;

    need = WideCharToMultiByte(CP_UTF8, 0, w, (int)wlen, NULL, 0, NULL, NULL);
    if (need <= 0) return BXL_FALSE;
    if (!bxl_buf_reserve(b, (size_t)need)) return BXL_FALSE;

    WideCharToMultiByte(CP_UTF8, 0, w, (int)wlen,
                        b->data + b->len, need, NULL, NULL);
    b->len += (size_t)need;
    b->data[b->len] = '\0';
    return BXL_TRUE;
}

int bxl_buf_append_utf16(BxlBuf *b, const wchar_t *w)
{
    if (!w) return BXL_TRUE;
    return bxl_buf_append_utf16n(b, w, wcslen(w));
}

char *bxl_buf_detach(BxlBuf *b, size_t *len_out)
{
    char *p;
    if (!b) return NULL;
    p = b->data;
    if (len_out) *len_out = b->len;
    b->data = NULL;
    b->len = b->cap = 0;
    return p;
}

/*==========================================================================
 * Strings
 *========================================================================*/
void bxl_str_trim(char *s)
{
    size_t n, i = 0;
    if (!s) return;
    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i > 0) memmove(s, s + i, n - i + 1);
}

size_t bxl_str_copy(char *dst, size_t dst_cch, const char *src)
{
    size_t n;
    if (!dst || dst_cch == 0) return 0;
    if (!src) { dst[0] = '\0'; return 0; }
    n = strlen(src);
    if (n >= dst_cch) n = dst_cch - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return n;
}

int bxl_str_ieq(const char *a, const char *b)
{
    if (!a || !b) return (a == b);
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return BXL_FALSE;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

int bxl_str_icontains(const char *hay, const char *needle)
{
    size_t nl;
    if (!hay || !needle) return BXL_FALSE;
    nl = strlen(needle);
    if (nl == 0) return BXL_TRUE;
    for (; *hay; hay++) {
        size_t i;
        for (i = 0; i < nl; i++) {
            char ca = hay[i], cb = needle[i];
            if (!ca) return BXL_FALSE;
            if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
            if (ca != cb) break;
        }
        if (i == nl) return BXL_TRUE;
    }
    return BXL_FALSE;
}

int bxl_str_startswith(const char *s, const char *prefix)
{
    size_t n;
    if (!s || !prefix) return BXL_FALSE;
    n = strlen(prefix);
    return (strncmp(s, prefix, n) == 0) ? BXL_TRUE : BXL_FALSE;
}

void bxl_str_tolower(char *s)
{
    if (!s) return;
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
}

/*==========================================================================
 * UTF-8 <-> UTF-16
 *========================================================================*/
int bxl_utf8_to_wide(const char *utf8, wchar_t *out, size_t out_cch)
{
    int n;
    if (!utf8 || !out || out_cch == 0) return BXL_FALSE;
    n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, (int)out_cch);
    if (n <= 0) { out[0] = L'\0'; return BXL_FALSE; }
    return BXL_TRUE;
}

int bxl_wide_to_utf8(const wchar_t *w, char *out, size_t out_cch)
{
    int n;
    if (!w || !out || out_cch == 0) return BXL_FALSE;
    n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)out_cch, NULL, NULL);
    if (n <= 0) { out[0] = '\0'; return BXL_FALSE; }
    return BXL_TRUE;
}

/*==========================================================================
 * Time
 *========================================================================*/
bxl_u64 bxl_now_ms(void)
{
    return (bxl_u64)GetTickCount64();
}

bxl_u64 bxl_unix_time(void)
{
    FILETIME ft;
    ULARGE_INTEGER u;
    GetSystemTimeAsFileTime(&ft);
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    /* 100ns ticks since 1601 -> seconds since 1970 */
    return (bxl_u64)((u.QuadPart / 10000000ULL) - 11644473600ULL);
}

static const char *k_wday_short[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
static const char *k_mon_short[12] = { "Jan","Feb","Mar","Apr","May","Jun",
                                       "Jul","Aug","Sep","Oct","Nov","Dec" };

static void split_utc(bxl_u64 t, SYSTEMTIME *st)
{
    ULARGE_INTEGER u;
    FILETIME ft;
    u.QuadPart = (t + 11644473600ULL) * 10000000ULL;
    ft.dwLowDateTime  = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    FileTimeToSystemTime(&ft, st);
}

int bxl_format_timestamp(bxl_u64 unix_sec, char *out, size_t out_cch)
{
    SYSTEMTIME st;
    split_utc(unix_sec, &st);
    return SUCCEEDED(StringCchPrintfA(out, out_cch, "%04d-%02d-%02d %02d:%02d:%02d",
                                      st.wYear, st.wMonth, st.wDay,
                                      st.wHour, st.wMinute, st.wSecond));
}

int bxl_format_timestamp_compact(bxl_u64 unix_sec, char *out, size_t out_cch)
{
    SYSTEMTIME st;
    split_utc(unix_sec, &st);
    return SUCCEEDED(StringCchPrintfA(out, out_cch, "%04d%02d%02d_%02d%02d%02d",
                                      st.wYear, st.wMonth, st.wDay,
                                      st.wHour, st.wMinute, st.wSecond));
}

int bxl_format_rfc5322(bxl_u64 unix_sec, char *out, size_t out_cch)
{
    SYSTEMTIME st;
    split_utc(unix_sec, &st);
    return SUCCEEDED(StringCchPrintfA(out, out_cch,
                                      "%s, %02d %s %04d %02d:%02d:%02d +0000",
                                      k_wday_short[st.wDayOfWeek % 7],
                                      st.wDay, k_mon_short[(st.wMonth - 1) % 12],
                                      st.wYear, st.wHour, st.wMinute, st.wSecond));
}

void bxl_local_components(bxl_u64 unix_sec, int *year, int *month, int *day,
                          int *hour, int *minute, int *second, int *wday)
{
    SYSTEMTIME utc, loc;
    split_utc(unix_sec, &utc);
    if (!SystemTimeToTzSpecificLocalTime(NULL, &utc, &loc))
        loc = utc;

    if (year)   *year   = loc.wYear;
    if (month)  *month  = loc.wMonth;
    if (day)    *day    = loc.wDay;
    if (hour)   *hour   = loc.wHour;
    if (minute) *minute = loc.wMinute;
    if (second) *second = loc.wSecond;
    if (wday)   *wday   = loc.wDayOfWeek;
}

bxl_u64 bxl_next_daily_utc(int hour, int minute, bxl_u64 now_sec)
{
    int ly, lm, ld, lh, lmin, ls, lw;
    SYSTEMTIME loc, utc;
    FILETIME ft;
    ULARGE_INTEGER u;
    bxl_u64 candidate;

    bxl_local_components(now_sec, &ly, &lm, &ld, &lh, &lmin, &ls, &lw);

    /* Build the candidate as local wall-clock, then convert back to UTC. */
    loc.wYear = (WORD)ly; loc.wMonth = (WORD)lm; loc.wDay = (WORD)ld;
    loc.wHour = (WORD)hour; loc.wMinute = (WORD)minute;
    loc.wSecond = 0; loc.wMilliseconds = 0;
    loc.wDayOfWeek = 0;

    if (!TzSpecificLocalTimeToSystemTime(NULL, &loc, &utc))
        return now_sec + 86400ULL;

    ft.dwLowDateTime  = utc.wMilliseconds ? utc.wMilliseconds : 0;
    {
        SYSTEMTIME tmp = utc;
        if (!SystemTimeToFileTime(&tmp, &ft)) return now_sec + 86400ULL;
    }
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    candidate = (bxl_u64)((u.QuadPart / 10000000ULL) - 11644473600ULL);

    if (candidate <= now_sec)
        candidate += 86400ULL;

    return candidate;
}

/*==========================================================================
 * Paths / files
 *========================================================================*/
int bxl_path_exe(wchar_t *out, size_t out_cch)
{
    DWORD n;
    if (!out || out_cch == 0) return BXL_FALSE;
    n = GetModuleFileNameW(NULL, out, (DWORD)out_cch);
    if (n == 0 || n >= out_cch) { out[0] = L'\0'; return BXL_FALSE; }
    return BXL_TRUE;
}

int bxl_path_temp_dir(wchar_t *out, size_t out_cch)
{
    DWORD n;
    if (!out || out_cch == 0) return BXL_FALSE;
    n = GetTempPathW((DWORD)out_cch, out);
    if (n == 0 || n >= out_cch) {
        if (!GetTempPathW((DWORD)out_cch, out)) { out[0] = L'\0'; return BXL_FALSE; }
    }
    /* Strip trailing separator for consistent joins. */
    n = (DWORD)wcslen(out);
    while (n > 0 && (out[n - 1] == L'\\' || out[n - 1] == L'/')) out[--n] = L'\0';
    return BXL_TRUE;
}

int bxl_path_join(wchar_t *out, size_t out_cch, const wchar_t *dir,
                  const wchar_t *name)
{
    if (!out || !dir || !name) return BXL_FALSE;
    if (SUCCEEDED(StringCchPrintfW(out, out_cch, L"%s\\%s", dir, name)))
        return BXL_TRUE;
    return BXL_FALSE;
}

int bxl_path_exists(const wchar_t *p)
{
    DWORD a;
    if (!p) return BXL_FALSE;
    a = GetFileAttributesW(p);
    return (a != INVALID_FILE_ATTRIBUTES) ? BXL_TRUE : BXL_FALSE;
}

int bxl_dir_create(const wchar_t *dir)
{
    wchar_t tmp[MAX_PATH * 2];
    size_t  i, n;

    if (!dir || !*dir) return BXL_FALSE;
    if (bxl_path_exists(dir)) return BXL_TRUE;

    if (FAILED(StringCchCopyW(tmp, BXL_COUNT_OF(tmp), dir))) return BXL_FALSE;
    n = wcslen(tmp);

    for (i = 3; i < n; i++) {
        if (tmp[i] == L'\\' || tmp[i] == L'/') {
            wchar_t save = tmp[i];
            tmp[i] = L'\0';
            if (wcslen(tmp) > 0 && !bxl_path_exists(tmp))
                CreateDirectoryW(tmp, NULL);
            tmp[i] = save;
        }
    }
    if (!CreateDirectoryW(tmp, NULL)) {
        DWORD e = GetLastError();
        if (e != ERROR_ALREADY_EXISTS) return BXL_FALSE;
    }
    return BXL_TRUE;
}

int bxl_file_append_flush(const wchar_t *path, const void *data, size_t len)
{
    HANDLE h;
    DWORD  written = 0;

    if (!path || !data || len == 0) return BXL_FALSE;

    h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return BXL_FALSE;

    if (!WriteFile(h, data, (DWORD)len, &written, NULL) || written != (DWORD)len) {
        CloseHandle(h);
        return BXL_FALSE;
    }
    /* Force to disk so a crash cannot lose the batch. */
    FlushFileBuffers(h);
    CloseHandle(h);
    return BXL_TRUE;
}

int bxl_file_read_all(const wchar_t *path, BxlBuf *out)
{
    HANDLE h;
    LARGE_INTEGER sz;
    DWORD  got = 0;
    char  *tmp;

    if (!path || !out) return BXL_FALSE;

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return BXL_FALSE;

    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0) {
        CloseHandle(h);
        return BXL_FALSE;
    }
    if (sz.QuadPart > (LONGLONG)(256 * 1024 * 1024)) {
        CloseHandle(h);
        return BXL_FALSE;
    }

    tmp = (char *)malloc((size_t)sz.QuadPart);
    if (!tmp) { CloseHandle(h); return BXL_FALSE; }

    if (!ReadFile(h, tmp, (DWORD)sz.QuadPart, &got, NULL)) {
        free(tmp);
        CloseHandle(h);
        return BXL_FALSE;
    }
    CloseHandle(h);

    if (!bxl_buf_append(out, tmp, got)) {
        free(tmp);
        return BXL_FALSE;
    }
    free(tmp);
    return BXL_TRUE;
}

bxl_u64 bxl_file_size(const wchar_t *path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!path) return 0;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) return 0;
    return ((bxl_u64)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
}

int bxl_file_delete(const wchar_t *path)
{
    if (!path) return BXL_FALSE;
    return DeleteFileW(path) ? BXL_TRUE : BXL_FALSE;
}

int bxl_file_mtime(const wchar_t *path, bxl_u64 *unix_out)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    ULARGE_INTEGER u;

    if (!path) return BXL_FALSE;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) return BXL_FALSE;

    u.LowPart  = fad.ftLastWriteTime.dwLowDateTime;
    u.HighPart = fad.ftLastWriteTime.dwHighDateTime;
    if (unix_out)
        *unix_out = (bxl_u64)((u.QuadPart / 10000000ULL) - 11644473600ULL);
    return BXL_TRUE;
}

int bxl_purge_old_files(const wchar_t *dir, const wchar_t *prefix,
                        bxl_u32 age_days, bxl_u32 *deleted_out)
{
    WIN32_FIND_DATAW fd;
    HANDLE h;
    wchar_t pattern[MAX_PATH * 2];
    bxl_u64 now, cutoff;
    bxl_u32 deleted = 0;

    if (deleted_out) *deleted_out = 0;
    if (!dir) return BXL_FALSE;

    if (SUCCEEDED(StringCchPrintfW(pattern, BXL_COUNT_OF(pattern), L"%s\\*",
                                   dir))) {
        /* fall through */
    } else {
        return BXL_FALSE;
    }

    now    = bxl_unix_time();
    cutoff = (age_days > 0) ? (bxl_u64)age_days * 86400ULL : 0;
    if (now > cutoff) cutoff = now - cutoff; else cutoff = 0;

    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return BXL_FALSE;

    do {
        wchar_t full[MAX_PATH * 2];
        bxl_u64 mt;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        /* Only touch files carrying our own prefix - never anything else. */
        if (prefix && prefix[0]) {
            if (wcsncmp(fd.cFileName, prefix, wcslen(prefix)) != 0) continue;
        }

        if (FAILED(StringCchPrintfW(full, BXL_COUNT_OF(full), L"%s\\%s",
                                    dir, fd.cFileName)))
            continue;

        if (!bxl_file_mtime(full, &mt)) continue;
        if (mt >= cutoff) continue;

        if (DeleteFileW(full)) deleted++;
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    if (deleted_out) *deleted_out = deleted;
    return BXL_TRUE;
}

/*==========================================================================
 * Machine identity
 *========================================================================*/
int bxl_hostname(char *out, size_t out_cch)
{
    char buf[256];
    DWORD n = (DWORD)sizeof(buf);
    if (!out || out_cch == 0) return BXL_FALSE;
    if (!GetComputerNameExA(ComputerNameDnsHostname, buf, &n)) {
        if (!GetComputerNameA(buf, &n)) { bxl_str_copy(out, out_cch, "unknown"); return BXL_FALSE; }
    }
    bxl_str_copy(out, out_cch, buf);
    return BXL_TRUE;
}

int bxl_username(char *out, size_t out_cch)
{
    char buf[256];
    DWORD n = (DWORD)sizeof(buf);
    if (!out || out_cch == 0) return BXL_FALSE;
    if (!GetUserNameA(buf, &n)) { bxl_str_copy(out, out_cch, "unknown"); return BXL_FALSE; }
    bxl_str_copy(out, out_cch, buf);
    return BXL_TRUE;
}

/*==========================================================================
 * Diagnostics
 *========================================================================*/
static int     g_log_to_file = 0;
static wchar_t g_log_dir[MAX_PATH * 2] = L"";
static CRITICAL_SECTION g_log_cs;
static LONG    g_log_cs_ready = 0;

void bxl_log_enable_file(int enable)
{
    if (InterlockedCompareExchange(&g_log_cs_ready, 1, 0) == 0) {
        InitializeCriticalSection(&g_log_cs);
    }
    EnterCriticalSection(&g_log_cs);
    g_log_to_file = enable;
    LeaveCriticalSection(&g_log_cs);
}

void bxl_log_set_dir(const wchar_t *dir)
{
    if (InterlockedCompareExchange(&g_log_cs_ready, 1, 0) == 0) {
        InitializeCriticalSection(&g_log_cs);
    }
    EnterCriticalSection(&g_log_cs);
    if (dir) StringCchCopyW(g_log_dir, BXL_COUNT_OF(g_log_dir), dir);
    else     g_log_dir[0] = L'\0';
    LeaveCriticalSection(&g_log_cs);
}

void bxl_logf(const char *fmt, ...)
{
    char    line[2048];
    va_list ap;
    int     n;
    char    stamp[32];

    va_start(ap, fmt);
    n = _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n < 0) return;

    OutputDebugStringA("[" BXL_PRODUCT_NAME "] ");
    OutputDebugStringA(line);
    OutputDebugStringA("\n");

    if (InterlockedCompareExchange(&g_log_cs_ready, 1, 0) == 0) {
        InitializeCriticalSection(&g_log_cs);
    }

    EnterCriticalSection(&g_log_cs);
    if (g_log_to_file && g_log_dir[0]) {
        wchar_t path[MAX_PATH * 2];
        BxlBuf  b;
        bxl_format_timestamp(bxl_unix_time(), stamp, sizeof(stamp));

        if (SUCCEEDED(StringCchPrintfW(path, BXL_COUNT_OF(path),
                                       L"%s\\debug.log", g_log_dir))) {
            if (bxl_buf_init(&b, 512)) {
                bxl_buf_appendf(&b, "[%s] %s\r\n", stamp, line);
                bxl_file_append_flush(path, b.data, b.len);
                bxl_buf_free(&b);
            }
        }
    }
    LeaveCriticalSection(&g_log_cs);
}
