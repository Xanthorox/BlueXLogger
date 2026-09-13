/*============================================================================
 * BlueXLogger - tests/test_format.c
 * Keystroke formatter unit tests.
 *
 * Created by Xencode-CLI by xanthorox
 *
 * Everything here drives bxl_fmt_apply() with synthetic BxlKeyEvent records
 * and asserts the exact readable output. No keyboard, no message pump, no
 * Windows hook is involved - the formatter is a pure function of its inputs,
 * which is the whole reason it was factored that way.
 *==========================================================================*/
#include "tests.h"
#include "bxl_format.h"
#include <ctype.h>

/*==========================================================================
 * Small helpers
 *
 * NOTE ON VK CODES. Real low-level hooks deliver letters as the upper-case
 * virtual-key codes VK_A..VK_Z (0x41..0x5A); case is carried by the Shift and
 * CapsLock state, never by the code. Never pass a lowercase character literal
 * here: 0x61..0x7A overlap VK_NUMPAD0..9 (0x60-0x69),
 * VK_MULTIPLY/ADD/SEPARATOR/SUBTRACT/DECIMAL/DIVIDE (0x6A-0x6F) and
 * VK_F1..VK_F11 (0x70-0x7A), so 'a' would silently become a numpad digit and
 * 'q' a function key. Use VK_A..VK_Z, or the up() helper below for strings.
 *========================================================================*/

/* Character literal -> the VK code a real hook would deliver. */
static bxl_u16 up(char c)
{
    return (bxl_u16)(unsigned char)toupper((unsigned char)c);
}

/* One key event. */
static BxlKeyEvent mk(bxl_u16 vk, int down)
{
    BxlKeyEvent e;
    memset(&e, 0, sizeof(e));
    e.vk      = vk;
    e.is_down = (bxl_u8)(down ? 1 : 0);
    return e;
}

/* Fresh formatter state + a cleared output buffer. */
static void fresh(BxlModState *st, BxlFmtOptions *opt, BxlBuf *out)
{
    bxl_fmt_default_options(opt);
    bxl_fmt_init(st, opt);
    bxl_buf_reset(out);
}

/* A complete press: key down followed by key up. */
static int tap(BxlModState *st, const BxlFmtOptions *opt, BxlBuf *out, bxl_u16 vk)
{
    BxlKeyEvent d = mk(vk, 1);
    BxlKeyEvent u = mk(vk, 0);
    int n = 0;
    n += bxl_fmt_apply(st, opt, &d, out);
    n += bxl_fmt_apply(st, opt, &u, out);
    return n;
}

/* Tap a key the active layout reports as dead (see BxlKeyEvent.is_dead).
 * The hook sets this from ToUnicodeEx; a synthetic event has to say so. */
static int tap_dead(BxlModState *st, const BxlFmtOptions *opt, BxlBuf *out,
                    bxl_u16 vk)
{
    BxlKeyEvent d = mk(vk, 1);
    BxlKeyEvent u = mk(vk, 0);
    int n = 0;
    d.is_dead = 1;
    n += bxl_fmt_apply(st, opt, &d, out);
    n += bxl_fmt_apply(st, opt, &u, out);
    return n;
}

/* Hold a modifier down (tap() would release it again immediately). */
static int hold(BxlModState *st, const BxlFmtOptions *opt, BxlBuf *out, bxl_u16 vk)
{
    BxlKeyEvent d = mk(vk, 1);
    return bxl_fmt_apply(st, opt, &d, out);
}

static int release(BxlModState *st, const BxlFmtOptions *opt, BxlBuf *out, bxl_u16 vk)
{
    BxlKeyEvent u = mk(vk, 0);
    return bxl_fmt_apply(st, opt, &u, out);
}

/*==========================================================================
 * Printable keys and case
 *========================================================================*/
static void t_printable(void)
{
    BxlModState st;
    BxlFmtOptions opt;
    BxlBuf out;

    bxl_buf_init(&out, 128);
    fresh(&st, &opt, &out);

    t_begin("plain letters are lower case");
    tap(&st, &opt, &out, 'H');
    tap(&st, &opt, &out, 'I');
    T_STR(out.data, "hi");

    fresh(&st, &opt, &out);
    t_begin("shift produces upper case");
    hold(&st, &opt, &out, VK_SHIFT);
    tap(&st, &opt, &out, 'H');
    release(&st, &opt, &out, VK_SHIFT);
    tap(&st, &opt, &out, 'I');
    T_STR(out.data, "Hi");

    fresh(&st, &opt, &out);
    t_begin("digits and their shifted symbols");
    tap(&st, &opt, &out, '1');
    hold(&st, &opt, &out, VK_SHIFT);
    tap(&st, &opt, &out, '1');
    release(&st, &opt, &out, VK_SHIFT);
    T_STR(out.data, "1!");

    fresh(&st, &opt, &out);
    t_begin("space is a literal blank");
    tap(&st, &opt, &out, 'A');
    tap(&st, &opt, &out, VK_SPACE);
    tap(&st, &opt, &out, 'B');
    T_STR(out.data, "a b");

    fresh(&st, &opt, &out);
    t_begin("OEM keys use their US glyphs");
    tap(&st, &opt, &out, VK_OEM_1);      /* ; */
    tap(&st, &opt, &out, VK_OEM_2);      /* / */
    tap(&st, &opt, &out, VK_OEM_3);      /* ` */
    tap(&st, &opt, &out, VK_OEM_4);      /* [ */
    tap(&st, &opt, &out, VK_OEM_5);      /* \ */
    tap(&st, &opt, &out, VK_OEM_6);      /* ] */
    tap(&st, &opt, &out, VK_OEM_7);      /* ' */
    tap(&st, &opt, &out, VK_OEM_COMMA);  /* , */
    tap(&st, &opt, &out, VK_OEM_PERIOD); /* . */
    tap(&st, &opt, &out, VK_OEM_MINUS);  /* - */
    tap(&st, &opt, &out, VK_OEM_PLUS);   /* = */
    T_STR(out.data, ";/`[\\]',.-=");

    fresh(&st, &opt, &out);
    t_begin("shifted OEM keys");
    hold(&st, &opt, &out, VK_SHIFT);
    tap(&st, &opt, &out, VK_OEM_1);      /* : */
    tap(&st, &opt, &out, VK_OEM_2);      /* ? */
    tap(&st, &opt, &out, VK_OEM_4);      /* { */
    tap(&st, &opt, &out, VK_OEM_6);      /* } */
    tap(&st, &opt, &out, VK_OEM_7);      /* " */
    release(&st, &opt, &out, VK_SHIFT);
    T_STR(out.data, ":?{}\"");

    bxl_buf_free(&out);
}

/*==========================================================================
 * CapsLock interaction
 *========================================================================*/
static void t_capslock(void)
{
    BxlModState st;
    BxlFmtOptions opt;
    BxlBuf out;

    bxl_buf_init(&out, 128);
    fresh(&st, &opt, &out);

    t_begin("capslock toggles and reports itself");
    tap(&st, &opt, &out, VK_CAPITAL);
    T_STR(out.data, "<CAPSLOCK>");
    T_INT(st.capslock, 1);

    t_begin("capslock upper-cases letters");
    tap(&st, &opt, &out, 'A');
    tap(&st, &opt, &out, 'B');
    T_STR(out.data, "<CAPSLOCK>AB");

    t_begin("capslock and shift cancel out");
    hold(&st, &opt, &out, VK_SHIFT);
    tap(&st, &opt, &out, 'C');
    release(&st, &opt, &out, VK_SHIFT);
    T_STR(out.data, "<CAPSLOCK>ABc");

    t_begin("capslock does not affect digits");
    tap(&st, &opt, &out, '5');
    T_STR(out.data, "<CAPSLOCK>ABc5");

    t_begin("second capslock press turns it back off");
    tap(&st, &opt, &out, VK_CAPITAL);
    T_INT(st.capslock, 0);
    tap(&st, &opt, &out, 'D');
    T_STR(out.data, "<CAPSLOCK>ABc5<CAPSLOCK>d");

    t_begin("GetKeyState seeding is honoured");
    fresh(&st, &opt, &out);
    bxl_fmt_set_locks(&st, 1, 1, 0);
    T_INT(st.capslock, 1);
    T_INT(st.numlock, 1);
    tap(&st, &opt, &out, 'E');
    T_STR(out.data, "E");

    bxl_buf_free(&out);
}

/*==========================================================================
 * Numpad and function keys
 *========================================================================*/
static void t_specials(void)
{
    BxlModState st;
    BxlFmtOptions opt;
    BxlBuf out;

    bxl_buf_init(&out, 256);
    fresh(&st, &opt, &out);

    t_begin("control keys render as tokens");
    tap(&st, &opt, &out, VK_RETURN);
    tap(&st, &opt, &out, VK_TAB);
    tap(&st, &opt, &out, VK_ESCAPE);
    tap(&st, &opt, &out, VK_BACK);
    tap(&st, &opt, &out, VK_DELETE);
    T_STR(out.data, "<ENTER><TAB><ESC><BACKSPACE><DEL>");

    fresh(&st, &opt, &out);
    t_begin("navigation keys render as tokens");
    tap(&st, &opt, &out, VK_UP);
    tap(&st, &opt, &out, VK_DOWN);
    tap(&st, &opt, &out, VK_LEFT);
    tap(&st, &opt, &out, VK_RIGHT);
    tap(&st, &opt, &out, VK_HOME);
    tap(&st, &opt, &out, VK_END);
    tap(&st, &opt, &out, VK_PRIOR);
    tap(&st, &opt, &out, VK_NEXT);
    T_STR(out.data, "<UP><DOWN><LEFT><RIGHT><HOME><END><PGUP><PGDN>");

    fresh(&st, &opt, &out);
    t_begin("function keys render as Fn");
    tap(&st, &opt, &out, VK_F1);
    tap(&st, &opt, &out, VK_F5);
    tap(&st, &opt, &out, VK_F12);
    T_STR(out.data, "<F1><F5><F12>");

    fresh(&st, &opt, &out);
    t_begin("numpad digits need numlock to be printable");
    /* With NumLock off the digit is not printable, so the key falls back to
     * its canonical token - the same name a combo would use. */
    tap(&st, &opt, &out, VK_NUMPAD1);
    tap(&st, &opt, &out, VK_NUMPAD2);
    T_STR(out.data, "<NUM1><NUM2>");
    bxl_fmt_set_locks(&st, 0, 1, 0);
    bxl_buf_reset(&out);
    tap(&st, &opt, &out, VK_NUMPAD1);
    tap(&st, &opt, &out, VK_NUMPAD2);
    tap(&st, &opt, &out, VK_ADD);
    tap(&st, &opt, &out, VK_SUBTRACT);
    tap(&st, &opt, &out, VK_MULTIPLY);
    tap(&st, &opt, &out, VK_DIVIDE);
    T_STR(out.data, "12+-*/");

    fresh(&st, &opt, &out);
    t_begin("print screen renders as PRTSC");
    tap(&st, &opt, &out, VK_SNAPSHOT);
    T_STR(out.data, "<PRTSC>");

    bxl_buf_free(&out);
}

/*==========================================================================
 * Modifier combinations
 *========================================================================*/
static void t_combos(void)
{
    BxlModState st;
    BxlFmtOptions opt;
    BxlBuf out;

    bxl_buf_init(&out, 128);

    fresh(&st, &opt, &out);
    t_begin("ctrl+c is a combo, not a bare c");
    hold(&st, &opt, &out, VK_CONTROL);
    tap(&st, &opt, &out, 'C');
    release(&st, &opt, &out, VK_CONTROL);
    T_STR(out.data, "<CTRL+C>");

    fresh(&st, &opt, &out);
    t_begin("alt+tab is a combo");
    hold(&st, &opt, &out, VK_MENU);
    tap(&st, &opt, &out, VK_TAB);
    release(&st, &opt, &out, VK_MENU);
    T_STR(out.data, "<ALT+TAB>");

    fresh(&st, &opt, &out);
    t_begin("win+r is a combo");
    hold(&st, &opt, &out, VK_LWIN);
    tap(&st, &opt, &out, 'R');
    release(&st, &opt, &out, VK_LWIN);
    T_STR(out.data, "<WIN+R>");

    fresh(&st, &opt, &out);
    t_begin("modifier order is CTRL ALT SHIFT WIN");
    hold(&st, &opt, &out, VK_LWIN);
    hold(&st, &opt, &out, VK_SHIFT);
    hold(&st, &opt, &out, VK_MENU);
    hold(&st, &opt, &out, VK_CONTROL);
    tap(&st, &opt, &out, VK_ESCAPE);
    release(&st, &opt, &out, VK_CONTROL);
    release(&st, &opt, &out, VK_MENU);
    release(&st, &opt, &out, VK_SHIFT);
    release(&st, &opt, &out, VK_LWIN);
    T_STR(out.data, "<CTRL+ALT+SHIFT+WIN+ESC>");

    fresh(&st, &opt, &out);
    t_begin("modifiers alone emit nothing");
    tap(&st, &opt, &out, VK_SHIFT);
    tap(&st, &opt, &out, VK_CONTROL);
    tap(&st, &opt, &out, VK_MENU);
    T_STR(out.data, "");

    fresh(&st, &opt, &out);
    t_begin("combo mode can be turned off");
    bxl_fmt_default_options(&opt);
    opt.show_modifier_combos = 0;
    bxl_fmt_init(&st, &opt);
    hold(&st, &opt, &out, VK_CONTROL);
    tap(&st, &opt, &out, 'C');
    release(&st, &opt, &out, VK_CONTROL);
    T_STR(out.data, "c");

    bxl_buf_free(&out);
}

/*==========================================================================
 * Auto-repeat suppression
 *========================================================================*/
static void t_repeat(void)
{
    BxlModState st;
    BxlFmtOptions opt;
    BxlBuf out;
    BxlKeyEvent d;
    int i;

    bxl_buf_init(&out, 128);

    fresh(&st, &opt, &out);
    t_begin("a held special key is reported once");
    d = mk(VK_LEFT, 1);
    for (i = 0; i < 5; i++)
        bxl_fmt_apply(&st, &opt, &d, &out);
    T_STR(out.data, "<LEFT>");

    t_begin("releasing the key re-arms it");
    release(&st, &opt, &out, VK_LEFT);
    for (i = 0; i < 3; i++)
        bxl_fmt_apply(&st, &opt, &d, &out);
    T_STR(out.data, "<LEFT><LEFT>");

    fresh(&st, &opt, &out);
    t_begin("suppression can be turned off");
    bxl_fmt_default_options(&opt);
    opt.suppress_special_repeats = 0;
    bxl_fmt_init(&st, &opt);
    for (i = 0; i < 3; i++)
        bxl_fmt_apply(&st, &opt, &d, &out);
    T_STR(out.data, "<LEFT><LEFT><LEFT>");

    fresh(&st, &opt, &out);
    t_begin("printable auto-repeat is not suppressed");
    d = mk('A', 1);
    for (i = 0; i < 3; i++)
        bxl_fmt_apply(&st, &opt, &d, &out);
    T_STR(out.data, "aaa");

    bxl_buf_free(&out);
}

/*==========================================================================
 * Dead keys
 *========================================================================*/
static void t_deadkeys(void)
{
    BxlModState st;
    BxlFmtOptions opt;
    BxlBuf out;

    bxl_buf_init(&out, 128);
    fresh(&st, &opt, &out);

    t_begin("ambiguous codes are punctuation when the layout does not say dead");
    /* 0xBD/0xBE/0xBF/0xC0 are VK_OEM_MINUS/VK_OEM_PERIOD/VK_OEM_2/VK_OEM_3 on
     * a US layout and the acute/circumflex/tilde/grave dead keys on
     * US-International. With no layout signal the printable glyph wins, which
     * is what a plain US keyboard must produce. */
    tap(&st, &opt, &out, VK_OEM_MINUS);
    tap(&st, &opt, &out, VK_OEM_PERIOD);
    tap(&st, &opt, &out, VK_OEM_2);
    tap(&st, &opt, &out, VK_OEM_3);
    T_STR(out.data, "-./`");
    T_INT(st.dead_pending, 0);

    fresh(&st, &opt, &out);
    t_begin("the same codes arm a dead key when the layout says so");
    tap_dead(&st, &opt, &out, VK_OEM_3);      /* 0xC0 == VK_DEAD_GRAVE */
    T_INT(st.dead_pending, 0xC0);
    tap(&st, &opt, &out, 'A');
    T_STR(out.data, "\xC3\xA0");

    fresh(&st, &opt, &out);
    t_begin("dead acute + e composes to e-acute (U+00E9)");
    tap_dead(&st, &opt, &out, 0xBD);          /* VK_DEAD_ACUTE */
    tap(&st, &opt, &out, 'E');
    T_STR(out.data, "\xC3\xA9");
    T_INT(st.dead_pending, 0);

    fresh(&st, &opt, &out);
    t_begin("dead acute + shift+e composes to E-acute (U+00C9)");
    tap_dead(&st, &opt, &out, 0xBD);
    hold(&st, &opt, &out, VK_SHIFT);
    tap(&st, &opt, &out, 'E');
    release(&st, &opt, &out, VK_SHIFT);
    T_STR(out.data, "\xC3\x89");

    fresh(&st, &opt, &out);
    t_begin("dead acute + space yields the standalone glyph");
    tap_dead(&st, &opt, &out, 0xBD);
    tap(&st, &opt, &out, VK_SPACE);
    T_STR(out.data, "\xC2\xB4");

    fresh(&st, &opt, &out);
    t_begin("dead acute + unmapped letter flushes a marker");
    tap_dead(&st, &opt, &out, 0xBD);
    tap(&st, &opt, &out, 'Q');
    T_STR(out.data, "<DEAD:ACUTE>q");

    fresh(&st, &opt, &out);
    t_begin("two dead keys flush the first");
    tap_dead(&st, &opt, &out, 0xBD);          /* acute */
    tap_dead(&st, &opt, &out, 0xBE);     /* circumflex */
    tap(&st, &opt, &out, 'A');
    T_STR(out.data, "<DEAD:ACUTE>\xC3\xA2");   /* a-circumflex U+00E2 */

    fresh(&st, &opt, &out);
    t_begin("dead grave + a composes to a-grave (U+00E0)");
    tap_dead(&st, &opt, &out, 0xC0);     /* VK_DEAD_GRAVE */
    tap(&st, &opt, &out, 'A');
    T_STR(out.data, "\xC3\xA0");

    fresh(&st, &opt, &out);
    t_begin("composition can be turned off");
    bxl_fmt_default_options(&opt);
    opt.compose_dead_keys = 0;
    bxl_fmt_init(&st, &opt);
    tap_dead(&st, &opt, &out, 0xBD);
    tap(&st, &opt, &out, 'E');
    T_STR(out.data, "<DEAD:ACUTE>e");

    bxl_buf_free(&out);
}

/*==========================================================================
 * Injected events
 *========================================================================*/
static void t_injected(void)
{
    BxlModState st;
    BxlFmtOptions opt;
    BxlBuf out;
    BxlKeyEvent e;

    bxl_buf_init(&out, 64);
    fresh(&st, &opt, &out);

    /* Injected input is captured by default. An RDP session, a VM's input
     * path, vendor keyboard software and the on-screen keyboard all deliver
     * keystrokes through SendInput, so treating "injected" as "synthetic and
     * uninteresting" drops real typing - in an RDP session it drops all of it.
     * Nothing here injects input, so there is no self-logging to avoid. */
    t_begin("injected events are captured by default");
    e = mk('X', 1);
    e.injected = 1;
    bxl_fmt_apply(&st, &opt, &e, &out);
    T_STR(out.data, "x");

    t_begin("injected events can be excluded on request");
    bxl_fmt_default_options(&opt);
    opt.track_injected = 0;
    bxl_fmt_init(&st, &opt);
    bxl_buf_reset(&out);
    bxl_fmt_apply(&st, &opt, &e, &out);
    T_STR(out.data, "");

    bxl_buf_free(&out);
}

/*==========================================================================
 * vk_name
 *========================================================================*/
static void t_vkname(void)
{
    char name[64];

    t_begin("vk_name covers letters, digits, Fn, numpad and tokens");
    T_OK(bxl_fmt_vk_name('Q', name, sizeof(name)) && strcmp(name, "Q") == 0);
    T_OK(bxl_fmt_vk_name('7', name, sizeof(name)) && strcmp(name, "7") == 0);
    T_OK(bxl_fmt_vk_name(VK_F1, name, sizeof(name)) && strcmp(name, "F1") == 0);
    T_OK(bxl_fmt_vk_name(VK_F12, name, sizeof(name)) && strcmp(name, "F12") == 0);
    T_OK(bxl_fmt_vk_name(VK_F24, name, sizeof(name)) && strcmp(name, "F24") == 0);
    T_OK(bxl_fmt_vk_name(VK_NUMPAD0, name, sizeof(name)) && strcmp(name, "NUM0") == 0);
    T_OK(bxl_fmt_vk_name(VK_NUMPAD9, name, sizeof(name)) && strcmp(name, "NUM9") == 0);
    T_OK(bxl_fmt_vk_name(VK_RETURN, name, sizeof(name)) && strcmp(name, "ENTER") == 0);
    T_OK(bxl_fmt_vk_name(VK_BACK, name, sizeof(name)) && strcmp(name, "BACKSPACE") == 0);
    T_OK(bxl_fmt_vk_name(VK_PRIOR, name, sizeof(name)) && strcmp(name, "PGUP") == 0);
    T_OK(bxl_fmt_vk_name(VK_OEM_1, name, sizeof(name)) && strcmp(name, ";") == 0);

    t_begin("is_modifier / is_dead_key classify correctly");
    T_OK(bxl_fmt_is_modifier(VK_SHIFT) == BXL_TRUE);
    T_OK(bxl_fmt_is_modifier(VK_LCONTROL) == BXL_TRUE);
    T_OK(bxl_fmt_is_modifier(VK_RMENU) == BXL_TRUE);
    T_OK(bxl_fmt_is_modifier(VK_LWIN) == BXL_TRUE);
    T_OK(bxl_fmt_is_modifier('A') == BXL_FALSE);
    T_OK(bxl_fmt_is_dead_key(0xBD) == BXL_TRUE);
    T_OK(bxl_fmt_is_dead_key(0xC0) == BXL_TRUE);
    T_OK(bxl_fmt_is_dead_key('A') == BXL_FALSE);
}

/*==========================================================================
 * Clipboard + context header
 *========================================================================*/
static void t_clipboard_and_header(void)
{
    BxlBuf out;

    bxl_buf_init(&out, 128);

    t_begin("clipboard payload is wrapped and newline-escaped");
    bxl_fmt_clipboard(&out, "a\nb", 3);
    T_STR(out.data, "<CLIPBOARD>a\\nb</CLIPBOARD>");

    t_begin("clipboard strips carriage returns but keeps the escaped newline");
    bxl_buf_reset(&out);
    bxl_fmt_clipboard(&out, "x\r\ny", 4);
    T_STR(out.data, "<CLIPBOARD>x\\ny</CLIPBOARD>");

    t_begin("clipboard handles empty input");
    bxl_buf_reset(&out);
    bxl_fmt_clipboard(&out, "", 0);
    T_STR(out.data, "<CLIPBOARD></CLIPBOARD>");

    t_begin("context header matches the documented layout");
    bxl_buf_reset(&out);
    bxl_fmt_context_header(&out, "2026-09-13 14:32:07",
                           "chrome.exe", "Inbox - Gmail");
    T_STR(out.data,
          "[2026-09-13 14:32:07] [chrome.exe \xE2\x80\x94 \"Inbox - Gmail\"]");

    t_begin("context header sanitises quotes and newlines in the title");
    bxl_buf_reset(&out);
    bxl_fmt_context_header(&out, "T", "p.exe", "a\"b\nc");
    T_STR(out.data, "[T] [p.exe \xE2\x80\x94 \"a'b c\"]");

    t_begin("context header tolerates a missing process name");
    bxl_buf_reset(&out);
    bxl_fmt_context_header(&out, "T", "", "Title");
    T_STR(out.data, "[T] [unknown \xE2\x80\x94 \"Title\"]");

    bxl_buf_free(&out);
}

/*==========================================================================
 * End-to-end: the exact sample from the specification
 *========================================================================*/
static void t_spec_sample(void)
{
    static const char *k_world = "hello world";
    static const char *k_test  = "this is a test";
    BxlModState st;
    BxlFmtOptions opt;
    BxlBuf out;
    const char *p;
    int i;

    bxl_buf_init(&out, 256);
    fresh(&st, &opt, &out);

    t_begin("spec sample: hello world<ENTER>this is a test<BACKSPACE>x3 s");

    for (p = k_world; *p; p++) tap(&st, &opt, &out, up(*p));
    tap(&st, &opt, &out, VK_RETURN);
    for (p = k_test; *p; p++) tap(&st, &opt, &out, up(*p));
    for (i = 0; i < 3; i++)   tap(&st, &opt, &out, VK_BACK);
    tap(&st, &opt, &out, 'S');

    T_STR(out.data,
          "hello world<ENTER>this is a test<BACKSPACE><BACKSPACE><BACKSPACE>s");

    bxl_buf_free(&out);
}

/*==========================================================================
 * Suite entry point
 *========================================================================*/
void test_format(void)
{
    t_suite("formatter");
    t_printable();
    t_capslock();
    t_specials();
    t_combos();
    t_repeat();
    t_deadkeys();
    t_injected();
    t_vkname();
    t_clipboard_and_header();
    t_spec_sample();
}
