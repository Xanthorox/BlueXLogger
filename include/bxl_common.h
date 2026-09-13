/*============================================================================
 * BlueXLogger - bxl_common.h
 * Common includes, compiler hygiene and shared scalar typedefs.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#ifndef BXL_COMMON_H
#define BXL_COMMON_H

/* ---------------------------------------------------------------------------
 * winsock2.h must precede windows.h, otherwise winsock.h gets pulled in and
 * the two definitions collide.
 * -------------------------------------------------------------------------*/
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
/* The whole codebase uses explicit W/A suffixes, so UNICODE only affects
 * the generic macros (RT_RCDATA, MAKEINTRESOURCE, ...). Unicode is correct. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00   /* Windows 10 / 11 */
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

/* SSPI/SChannel is used by the network layer for TLS. */
#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif

/* This is a C project, so COM interfaces are driven through the generated
 * <Interface>_<Method> macros. COBJMACROS must precede every COM header. */
#ifndef COBJMACROS
#define COBJMACROS
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <sspi.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shellapi.h>
#include <strsafe.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "bxl_branding.h"

/* ---------------------------------------------------------------------------
 * Compiler hygiene
 * -------------------------------------------------------------------------*/
#if defined(_MSC_VER)
#  define BXL_INLINE      __inline
#  define BXL_NORETURN    __declspec(noreturn)
#  define BXL_PRINTF_FMT(a,b)
#  define BXL_UNUSED(x)   ((void)(x))
#else
#  define BXL_INLINE      inline
#  define BXL_NORETURN
#  define BXL_PRINTF_FMT(a,b) __attribute__((format(printf,a,b)))
#  define BXL_UNUSED(x)   ((void)(x))
#endif

#ifndef BXL_ASSERT
#  ifdef _DEBUG
#    include <assert.h>
#    define BXL_ASSERT(x) assert(x)
#  else
#    define BXL_ASSERT(x) ((void)0)
#  endif
#endif

/* ---------------------------------------------------------------------------
 * Shared scalar typedefs
 * -------------------------------------------------------------------------*/
typedef uint8_t   bxl_u8;
typedef uint16_t  bxl_u16;
typedef uint32_t  bxl_u32;
typedef uint64_t  bxl_u64;
typedef int8_t    bxl_i8;
typedef int16_t   bxl_i16;
typedef int32_t   bxl_i32;
typedef int64_t   bxl_i64;

#define BXL_TRUE   1
#define BXL_FALSE  0

#define BXL_COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

#ifndef BXL_MIN
#define BXL_MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef BXL_MAX
#define BXL_MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

#endif /* BXL_COMMON_H */
