/*============================================================================
 * BlueXLogger - tests/framework.c
 * Assertion framework implementation.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "tests.h"
#include <stdarg.h>

static int g_pass;
static int g_fail;
static int g_suite_pass;
static int g_suite_fail;
static const char *g_suite = "";
static int g_case_fail;

void t_suite(const char *name)
{
    if (g_suite[0]) {
        printf("  -> %s: %d passed, %d failed\n",
               g_suite, g_suite_pass, g_suite_fail);
    }
    g_suite      = name;
    g_suite_pass = 0;
    g_suite_fail = 0;
    printf("\n== %s ================================================\n", name);
}

void t_begin(const char *name)
{
    g_case_fail = 0;
    printf("  [ case ] %s\n", name);
}

void t_check(int ok, const char *expr, const char *file, int line)
{
    if (ok) {
        g_pass++;
        g_suite_pass++;
        return;
    }
    g_fail++;
    g_suite_fail++;
    g_case_fail++;
    printf("      FAIL %s:%d  %s\n", file, line, expr);
}

void t_checkf(int ok, const char *file, int line, const char *fmt, ...)
{
    if (ok) {
        g_pass++;
        g_suite_pass++;
        return;
    }
    g_fail++;
    g_suite_fail++;
    g_case_fail++;
    printf("      FAIL %s:%d  ", file, line);
    {
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }
    printf("\n");
}

void t_note(const char *fmt, ...)
{
    va_list ap;
    printf("      note: ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

void t_streq(const char *got, const char *want, const char *what,
             const char *file, int line)
{
    if (got && want && strcmp(got, want) == 0) {
        g_pass++;
        g_suite_pass++;
        return;
    }
    g_fail++;
    g_suite_fail++;
    g_case_fail++;
    printf("      FAIL %s:%d  %s\n", file, line, what);
    printf("        want: \"%s\"\n", want ? want : "(null)");
    printf("        got : \"%s\"\n", got  ? got  : "(null)");
}

void t_wstreq(const wchar_t *got, const wchar_t *want, const char *what,
              const char *file, int line)
{
    if (got && want && wcscmp(got, want) == 0) {
        g_pass++;
        g_suite_pass++;
        return;
    }
    g_fail++;
    g_suite_fail++;
    g_case_fail++;
    printf("      FAIL %s:%d  %s\n", file, line, what);
    printf("        want: \"%ls\"\n", want ? want : L"(null)");
    printf("        got : \"%ls\"\n", got  ? got  : L"(null)");
}

void t_mem(const void *got, size_t got_len, const void *want, size_t want_len,
           const char *what, const char *file, int line)
{
    if (got_len == want_len && got && want &&
        memcmp(got, want, want_len) == 0) {
        g_pass++;
        g_suite_pass++;
        return;
    }
    g_fail++;
    g_suite_fail++;
    g_case_fail++;
    printf("      FAIL %s:%d  %s (lengths %llu vs %llu)\n", file, line, what,
           (unsigned long long)got_len, (unsigned long long)want_len);
    if (got && want && got_len == want_len && got_len < 256) {
        size_t i;
        printf("        want:");
        for (i = 0; i < want_len; i++) printf(" %02X", ((const unsigned char *)want)[i]);
        printf("\n        got :");
        for (i = 0; i < got_len; i++) printf(" %02X", ((const unsigned char *)got)[i]);
        printf("\n");
    }
}

void t_summary(void)
{
    if (g_suite[0]) {
        printf("  -> %s: %d passed, %d failed\n",
               g_suite, g_suite_pass, g_suite_fail);
    }
    printf("\n======================================================\n");
    printf("  TOTAL: %d passed, %d failed\n", g_pass, g_fail);
    printf("  RESULT: %s\n", g_fail == 0 ? "PASS" : "FAIL");
    printf("======================================================\n");
}

int t_exit_code(void)
{
    return g_fail == 0 ? 0 : 1;
}
