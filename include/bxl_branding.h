/*============================================================================
 * BlueXLogger - bxl_branding.h
 * Product branding, watermark and attribution constants.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#ifndef BXL_BRANDING_H
#define BXL_BRANDING_H

#define BXL_PRODUCT_NAME      "BlueXLogger"
#define BXL_PRODUCT_NAME_W    L"BlueXLogger"
#define BXL_BUILDER_NAME      "BlueXBuilder"
#define BXL_BUILDER_NAME_W    L"BlueXBuilder"

/* The required watermark / attribution string. */
#define BXL_WATERMARK         "Created by Xencode-CLI by xanthorox"
#define BXL_WATERMARK_W       L"Created by Xencode-CLI by xanthorox"

#define BXL_VENDOR            "xanthorox"
#define BXL_VENDOR_W          L"xanthorox"

#define BXL_VERSION_MAJOR     1
#define BXL_VERSION_MINOR     0
#define BXL_VERSION_PATCH     0
#define BXL_VERSION_BUILD     0

#define BXL_VERSION_STR       "1.0.0.0"
#define BXL_VERSION_STR_W     L"1.0.0.0"

#define BXL_COPYRIGHT         "Copyright (C) 2026 xanthorox"
#define BXL_COPYRIGHT_W       L"Copyright (C) 2026 xanthorox"

#define BXL_DESCRIPTION       "BlueXLogger keystroke and screen capture agent"
#define BXL_DESCRIPTION_W     L"BlueXLogger keystroke and screen capture agent"

#define BXL_XMAILER           BXL_PRODUCT_NAME " " BXL_VERSION_STR " (" BXL_WATERMARK ")"

/* Source-file header stamp, used by every translation unit. */
#define BXL_FILE_HEADER \
    "BlueXLogger -- " BXL_WATERMARK

/* Default email subject prefix. */
#define BXL_DEFAULT_SUBJECT   BXL_PRODUCT_NAME " report"

#endif /* BXL_BRANDING_H */
