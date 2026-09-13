/*============================================================================
 * BlueXLogger - tests/test_screenshot.c
 * Desktop capture / encoding tests.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * These tests really capture the desktop and really encode through WIC, so
 * they assert on the produced bytes: the PNG signature, the IHDR dimensions
 * and the JPEG SOI marker. A file that merely exists would not prove much.
 *==========================================================================*/
#include "tests.h"
#include "bxl_screenshot.h"
#include "bxl_config.h"
#include "bxl_util.h"

static const unsigned char k_png_magic[8] =
    { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

/*==========================================================================
 * Helpers
 *========================================================================*/
static bxl_u32 be32(const unsigned char *p)
{
    return ((bxl_u32)p[0] << 24) | ((bxl_u32)p[1] << 16)
         | ((bxl_u32)p[2] << 8)  | (bxl_u32)p[3];
}

/* Pull the dimensions out of a PNG IHDR chunk. */
static int png_dims(const BxlShot *s, bxl_u32 *w, bxl_u32 *h)
{
    const unsigned char *p = (const unsigned char *)s->data;
    if (!p || s->len < 24) return 0;
    if (memcmp(p, k_png_magic, 8) != 0) return 0;
    if (memcmp(p + 12, "IHDR", 4) != 0) return 0;
    *w = be32(p + 16);
    *h = be32(p + 20);
    return 1;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    if (lx > ls) return 0;
    return memcmp(s + ls - lx, suffix, lx) == 0;
}

/*==========================================================================
 * Options mapping
 *========================================================================*/
static void t_options(void)
{
    BxlConfig c;
    BxlShotOptions o;

    t_begin("options are taken from the operator configuration");
    bxl_config_defaults(&c);
    c.shot_monitors     = BXL_MON_PRIMARY;
    c.shot_format       = BXL_FMT_JPEG;
    c.shot_jpeg_quality = 55;
    c.shot_max_dim      = 1280;
    bxl_screenshot_options_from(&c, &o);
    T_INT(o.monitors, BXL_MON_PRIMARY);
    T_INT(o.format, BXL_FMT_JPEG);
    T_INT(o.jpeg_quality, 55);
    T_INT(o.max_dim, 1280);

    t_begin("a zero quality falls back to the documented default");
    c.shot_jpeg_quality = 0;
    bxl_screenshot_options_from(&c, &o);
    T_INT(o.jpeg_quality, 80);
}

/*==========================================================================
 * Capture area
 *========================================================================*/
static void t_area(void)
{
    int x = 0, y = 0, w = 0, h = 0;
    int px = 0, py = 0, pw = 0, ph = 0;

    t_begin("the primary-only area is the primary monitor");
    T_OK(bxl_screenshot_area(BXL_MON_PRIMARY, &px, &py, &pw, &ph) == BXL_TRUE);
    T_INT(px, 0);
    T_INT(py, 0);
    T_INT(pw, GetSystemMetrics(SM_CXSCREEN));
    T_INT(ph, GetSystemMetrics(SM_CYSCREEN));
    T_OK(pw > 0 && ph > 0);

    t_begin("the all-monitors area is the virtual desktop");
    T_OK(bxl_screenshot_area(BXL_MON_ALL, &x, &y, &w, &h) == BXL_TRUE);
    T_OK(w > 0 && h > 0);
    T_INT(x, GetSystemMetrics(SM_XVIRTUALSCREEN));
    T_INT(y, GetSystemMetrics(SM_YVIRTUALSCREEN));
    T_INT(w, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    T_INT(h, GetSystemMetrics(SM_CYVIRTUALSCREEN));
    T_OK(w >= pw && h >= ph);
}

/*==========================================================================
 * PNG capture
 *========================================================================*/
static void t_png(void)
{
    BxlShotOptions opt;
    BxlShot shot;
    int x, y, w, h;
    bxl_u32 iw = 0, ih = 0;

    memset(&opt, 0, sizeof(opt));
    opt.monitors     = BXL_MON_ALL;
    opt.format       = BXL_FMT_PNG;
    opt.jpeg_quality = 80;
    opt.max_dim      = 0;      /* native resolution */

    t_begin("the capture engine initialises");
    T_OK(bxl_screenshot_init() == BXL_TRUE);
    T_OK(bxl_screenshot_ready() == BXL_TRUE);

    T_OK(bxl_screenshot_area(opt.monitors, &x, &y, &w, &h) == BXL_TRUE);

    t_begin("a PNG capture returns non-empty encoded data");
    memset(&shot, 0, sizeof(shot));
    T_OK(bxl_screenshot_capture(&opt, "testhost", &shot) == BXL_TRUE);
    T_OK(shot.data != NULL);
    T_OK(shot.len > 0);

    t_begin("the bytes are a real PNG of the expected dimensions");
    T_OK(memcmp(shot.data, k_png_magic, 8) == 0);
    T_OK(png_dims(&shot, &iw, &ih) == 1);
    T_INT(iw, (bxl_u32)w);
    T_INT(ih, (bxl_u32)h);
    T_INT(shot.width, w);
    T_INT(shot.height, h);

    t_begin("metadata is populated correctly");
    T_STR(shot.mime_type, "image/png");
    T_OK(strstr(shot.filename, BXL_PRODUCT_NAME) != NULL);
    T_OK(strstr(shot.filename, "testhost") != NULL);
    T_OK(ends_with(shot.filename, ".png") == 1);

    bxl_shot_free(&shot);
    T_OK(shot.data == NULL && shot.len == 0);

    bxl_screenshot_shutdown();
}

/*==========================================================================
 * JPEG capture + downscale
 *========================================================================*/
static void t_jpeg_and_downscale(void)
{
    BxlShotOptions opt;
    BxlShot shot;
    int x, y, w, h;
    bxl_u32 iw = 0, ih = 0;

    t_begin("the capture engine re-initialises cleanly");
    T_OK(bxl_screenshot_init() == BXL_TRUE);
    T_OK(bxl_screenshot_area(BXL_MON_ALL, &x, &y, &w, &h) == BXL_TRUE);

    t_begin("a JPEG capture produces a valid JPEG");
    memset(&opt, 0, sizeof(opt));
    opt.monitors     = BXL_MON_ALL;
    opt.format       = BXL_FMT_JPEG;
    opt.jpeg_quality = 70;
    opt.max_dim      = 0;

    memset(&shot, 0, sizeof(shot));
    T_OK(bxl_screenshot_capture(&opt, "testhost", &shot) == BXL_TRUE);
    T_OK(shot.len > 0);
    {
        const unsigned char *p = (const unsigned char *)shot.data;
        T_INT(p[0], 0xFF);
        T_INT(p[1], 0xD8);            /* JPEG SOI */
    }
    T_STR(shot.mime_type, "image/jpeg");
    T_OK(ends_with(shot.filename, ".jpg") == 1);
    bxl_shot_free(&shot);

    t_begin("max_dim downscales the longest edge in the encoded image");
    if (w > 320 || h > 320) {
        memset(&opt, 0, sizeof(opt));
        opt.monitors = BXL_MON_ALL;
        opt.format   = BXL_FMT_PNG;
        opt.max_dim  = 320;

        memset(&shot, 0, sizeof(shot));
        T_OK(bxl_screenshot_capture(&opt, "testhost", &shot) == BXL_TRUE);
        T_OK(png_dims(&shot, &iw, &ih) == 1);
        T_INT((iw > ih) ? iw : ih, 320);
        T_OK(iw <= (bxl_u32)w && ih <= (bxl_u32)h);
        bxl_shot_free(&shot);
    } else {
        t_note("skipped: desktop is smaller than the 320 px test target");
    }

    bxl_screenshot_shutdown();
}

/*==========================================================================
 * Capture to file
 *========================================================================*/
static void t_to_file(void)
{
    BxlShotOptions opt;
    wchar_t dir[MAX_PATH * 2];
    wchar_t path[MAX_PATH * 2];
    wchar_t tmp[MAX_PATH * 2];
    bxl_u64 size;
    int     x, y, w, h;

    t_begin("capture_to_file writes a named PNG to disk");
    if (!bxl_path_temp_dir(tmp, BXL_COUNT_OF(tmp))) {
        T_OK(0);
        return;
    }
    if (!bxl_path_join(dir, BXL_COUNT_OF(dir), tmp, L"BlueXLogger_test_shots")) {
        T_OK(0);
        return;
    }

    T_OK(bxl_screenshot_init() == BXL_TRUE);
    T_OK(bxl_screenshot_area(BXL_MON_PRIMARY, &x, &y, &w, &h) == BXL_TRUE);

    memset(&opt, 0, sizeof(opt));
    opt.monitors = BXL_MON_PRIMARY;
    opt.format   = BXL_FMT_PNG;
    opt.max_dim  = 640;

    path[0] = L'\0';
    T_OK(bxl_screenshot_capture_to_file(&opt, "testhost", dir,
                                        path, BXL_COUNT_OF(path)) == BXL_TRUE);
    T_OK(path[0] != L'\0');
    T_OK(bxl_path_exists(path) == BXL_TRUE);

    t_begin("the file on disk is a non-empty PNG with the right name");
    size = bxl_file_size(path);
    T_OK(size > 0);
    {
        BxlBuf b;
        bxl_buf_init(&b, 4096);
        T_OK(bxl_file_read_all(path, &b) == BXL_TRUE);
        T_OK(b.len >= 8);
        T_OK(memcmp(b.data, k_png_magic, 8) == 0);
        bxl_buf_free(&b);
    }
    {
        const wchar_t *slash = wcsrchr(path, L'\\');
        const wchar_t *name  = slash ? slash + 1 : path;
        T_OK(wcsstr(name, L"BlueXLogger") == name);
        T_OK(wcsstr(name, L"testhost") != NULL);
        T_OK(wcsstr(name, L".png") != NULL);
    }

    bxl_file_delete(path);
    bxl_screenshot_shutdown();
}

/*==========================================================================
 * Suite entry point
 *========================================================================*/
void test_screenshot(void)
{
    t_suite("screenshot");
    t_options();
    t_area();
    t_png();
    t_jpeg_and_downscale();
    t_to_file();
}
