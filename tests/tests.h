/*============================================================================
 * BlueXLogger - tests/tests.h
 * Minimal assertion framework for the BlueXLogger test suite.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Deliberately tiny: no external test framework, no extra dependencies
 * beyond the Windows SDK, so the suite builds with the same toolchain as the
 * product. Results are printed as they happen and summarised at the end.
 *==========================================================================*/
#ifndef BXL_TESTS_H
#define BXL_TESTS_H

#include "bxl_common.h"

/* ---- framework ----------------------------------------------------------*/
void t_suite(const char *name);
void t_begin(const char *name);
void t_check(int ok, const char *expr, const char *file, int line);
void t_checkf(int ok, const char *file, int line, const char *fmt, ...);
void t_streq(const char *got, const char *want, const char *what,
             const char *file, int line);
void t_wstreq(const wchar_t *got, const wchar_t *want, const char *what,
              const char *file, int line);
void t_note(const char *fmt, ...);
void t_summary(void);
int  t_exit_code(void);

#define T_OK(cond)       t_check((cond) ? 1 : 0, #cond, __FILE__, __LINE__)
#define T_INT(got, want) t_check((long long)(got) == (long long)(want), #got, \
                                 __FILE__, __LINE__)
#define T_STR(got, want) t_streq((got), (want), #got, __FILE__, __LINE__)
#define T_STR_W(got, want) t_wstreq((got), (want), #got, __FILE__, __LINE__)
#define T_MSG(cond, ...) t_checkf((cond) ? 1 : 0, __FILE__, __LINE__, __VA_ARGS__)

/* ---- suites -------------------------------------------------------------*/
void test_format(void);
void test_config(void);
void test_schedule(void);
void test_smtp(void);
void test_http(void);
void test_telegram(void);
void test_identity(void);
void test_spool(void);
void test_screenshot(void);

/* ---- shared state -------------------------------------------------------*/
/* Path to the freshly built payload EXE, passed on the command line by
 * run_tests.bat. Empty when the suite is run without one, in which case the
 * embedded-patch tests print a skip note instead of failing. */
extern wchar_t g_payload_exe[1024];

/* ---- shared helpers -----------------------------------------------------*/
/* Compare a byte buffer to a literal and report a readable diff. */
void t_mem(const void *got, size_t got_len, const void *want, size_t want_len,
           const char *what, const char *file, int line);
#define T_MEM(g, gl, w, wl) t_mem((g), (gl), (w), (wl), #g, __FILE__, __LINE__)

#endif /* BXL_TESTS_H */
