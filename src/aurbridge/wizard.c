/* ═══════════════════════════════════════════════════════════════════
 *  AurBridge wizard — the face of the only component of AurOS that can
 *  destroy a stranger's data.
 *
 *  Everything here is owner-drawn: there is not a single Win32 control
 *  in this program. A grey system button in the middle of a designed
 *  page reads as "unfinished software", and a user who does not trust
 *  the installer is a user who clicks through the disclosure without
 *  reading it. The look is load-bearing.
 *
 *  NOTHING IN THIS FILE OPENS A HANDLE, and none should ever be added.
 *  The work is in phases.c, behind plat.h, on a worker thread. That is
 *  not tidiness: this file's code runs on the UI thread, so anything
 *  that blocks stops the window painting, and a wizard that appears to
 *  have crashed in the middle of preparing somebody's disk is the
 *  worst thing this program can do short of losing her files.
 *
 *  Before the restart, everything AurBridge has done is undoable, and
 *  closing the window undoes it — see install_cancel().
 *
 *  Three screens before the progress screen, and nothing to type on any
 *  of them (docs/handoff/SESSION-2026-10-23.md, D3): the first page with
 *  the check running on it, the look, Ready. Then one bar, and Restart.
 *
 *  Safety invariants enforced in code, not just in pixels:
 *    - nav_allowed() refuses every page past the first unless preflight
 *      has completed with zero PF_BLOCK results. The BLOCKED page has no
 *      continue affordance, and even if one were added by accident,
 *      nav_allowed() would refuse it.
 *    - "Install AurOS" re-runs preflight before the phase list is
 *      touched (AURBRIDGE.md: destructive subcommands re-run preflight
 *      and abort on any block).
 *
 *  Build: build/aurbridge.
 * ═══════════════════════════════════════════════════════════════════ */

/* Vista+ locale APIs (GetLocaleInfoEx); everything newer than that is
 * reached through GetProcAddress so the binary still loads on Windows 7. */
#ifndef WINVER
#define WINVER       0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

/* C, not C++, view of the COM interfaces: the screen-reader object at
 * the end of this file is an IAccessible written out by hand. */
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <oleacc.h>
#include <shellapi.h>

/* OLEACC.DLL IS LOADED BY NAME FROM SYSTEM32 WHEN FIRST ASKED, NOT LINKED (SEC-003):
 * a linked DLL is resolved before WinMain from the folder the installer sits in. */
void *plat_load_system_dll(const char *name);
static HRESULT (WINAPI *p_AOFW)(HWND, DWORD, REFIID, void **);
static HRESULT (WINAPI *p_CSAO)(HWND, LONG, REFIID, void **);
static LRESULT (WINAPI *p_LFO)(REFIID, WPARAM, LPUNKNOWN);
static int oleacc_ready(void)
{
    if (p_AOFW) return 1;
    HMODULE m = (HMODULE)plat_load_system_dll("oleacc.dll");
    if (!m) return 0;
    p_CSAO = (void *)GetProcAddress(m, "CreateStdAccessibleObject");
    p_LFO  = (void *)GetProcAddress(m, "LresultFromObject");
    p_AOFW = (void *)GetProcAddress(m, "AccessibleObjectFromWindow");
    if (!p_CSAO || !p_LFO || !p_AOFW) { p_AOFW = NULL; return 0; }
    return 1;
}
static HRESULT acc_AccessibleObjectFromWindow(HWND h, DWORD id, REFIID r, void **pv)
{ return oleacc_ready() ? p_AOFW(h, id, r, pv) : E_FAIL; }
static HRESULT acc_CreateStdAccessibleObject(HWND h, LONG id, REFIID r, void **pv)
{ return oleacc_ready() ? p_CSAO(h, id, r, pv) : E_FAIL; }
static LRESULT acc_LresultFromObject(REFIID r, WPARAM w, LPUNKNOWN u)
{ return oleacc_ready() ? p_LFO(r, w, u) : 0; }
#define AccessibleObjectFromWindow acc_AccessibleObjectFromWindow
#define CreateStdAccessibleObject  acc_CreateStdAccessibleObject
#define LresultFromObject          acc_LresultFromObject
#include <shlobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#include <wctype.h>
#include <math.h>
#include <time.h>

#ifndef WM_WTSSESSION_CHANGE
#define WM_WTSSESSION_CHANGE 0x02B1
#endif
#ifndef WTS_SESSION_UNLOCK
#define WTS_SESSION_UNLOCK 0x8
#endif

#include "preflight.h"
#include "sbdb.h"
#include "phases.h"
#include "fix.h"
#include "hwinv.h"
#include "hwinv_win.h"
#include "hwcompat.h"

/* Baked in by build/aurbridge. A build that has not been told where
 * the image lives produces an installer that expects it to be sitting
 * beside itself, which is what a developer build is. */
#ifndef AUROS_IMAGE_URL
#define AUROS_IMAGE_URL     ""
#endif
#ifndef AUROS_IMAGE_SHA256
#define AUROS_IMAGE_SHA256  ""
#endif
#ifndef AUROS_IMAGE_BYTES
#define AUROS_IMAGE_BYTES   0ULL
#endif
/* WHERE THE IMAGE IS PUBLISHED IN PIECES, baked in by build/aurbridge
 * from AUROS_IMAGE_PIECES (see phases.h for the format). Empty in a
 * build that was not given one. */
#if defined(__has_include)
# if __has_include("aurbridge-baked.h")
#  include "aurbridge-baked.h"
# endif
#endif
#ifndef AUROS_PIECES_TEXT
#define AUROS_PIECES_TEXT   ""
#endif

/* ── Palette ──────────────────────────────────────────────────────────
 * docs/DESIGN-DIRECTION.md §3, "Light": the website's and AurOS's own
 * paper and ink, so the window that opens is plainly the thing she just
 * downloaded and the thing she will get.
 *
 * ONE ACCENT, GREEN, meaning one thing: the way forward, and what she
 * chose -- the primary button, the current step, the chosen look, the
 * mark. Nothing else is green. Warn is only "you need to do this", and
 * its text is ink. There is no disabled grey and no red.
 *
 * Text is at least 4.5:1 on every surface it sits on, and so is every
 * rule: a 2 px rule is what makes a thing look pressable. */
#define C_BG          0xF6F2E9u   /* paper: the page                    */
#define C_SOFT        0xECE6DAu   /* soft: the sidebar, a quiet panel   */
#define C_SURFACE     0xFCFAF5u   /* card: a button, a look's card      */
#define C_SELECTED    0xE1ECE7u   /* the chosen card only, never hover  */
#define C_RULE        0x6E675Cu   /* every border and outline: 5.0:1    */
#define C_INK         0x17181Au   /* all text she must read: 15.9:1     */
#define C_INK2        0x3A3D42u   /* secondary text, icons: 9.8:1       */
#define C_QUIET       0x5C6066u   /* legal lines only: 5.7:1            */
#define C_ACCENT      0x0B5D4Fu   /* go: THE primary button; the e2e driver
                                     finds it by this (tools/e2e/vision.py) */
#define C_ACCENT_DOWN 0x07463Bu   /* go, pressed                        */
#define C_ON_ACCENT   0xFFFFFFu   /* its label: 7.8:1                   */
#define C_WARN        0x8A5A12u   /* the icon and bar of "do this": 5.0 */
#define C_WARN_TINT   0xF7EBD2u   /* behind it                          */

#define CR(c) RGB(((c)>>16)&0xFF, ((c)>>8)&0xFF, (c)&0xFF)

/* ── How it will look: the one question she is asked ──────────────────
 *
 * The owner's words (docs/handoff/SESSION-2026-10-23.md): "AurOS where
 * she picks the style first: like Windows / like a Mac / big simple
 * buttons". So the cards are named after what she already knows. The
 * rule in docs/SHELLS.md that no other system is named holds for
 * AurOS's own screens; here, on Windows, before she has seen AurOS,
 * "like Windows" is the one description she can use, and EASY.md (every
 * word is one she uses) outranks it.
 *
 * Each card is a desktop (shells/<shell>.shell) and a look
 * (themes/<theme>.theme). Both travel in ab_choice to
 * \EFI\AurOS\choices.conf as shell= and theme=, and
 * rootfs/usr/lib/auros/choices.sh applies them at first boot. The
 * picture on each card is drawn in that theme's own colours, so what
 * she picks is what she gets.
 *
 * Sandstone, the light theme, for all four: dark text on a light
 * ground is what Windows and the Mac show her today, and it is the
 * easier one to read for most older eyes. Big and simple is Rail,
 * because docs/SHELLS.md says it is the one for "someone who has never
 * been comfortable with a computer": nothing in it can be hidden.
 *
 * PHASE 2 ("other systems") is another kind of card that leads to a
 * page of its own instead of setting these two values: a fifth card, or
 * a line under the row, and nothing else needs to move. */
enum { PIC_WINDOWS = 0, PIC_MAC, PIC_SIMPLE, PIC_ONE };

typedef struct {
    const char    *shell;     /* shell_archetype: shells/<id>.shell     */
    const char    *theme;     /* theme: themes/<id>.theme               */
    const wchar_t *name;      /* the card's name, in her words          */
    const wchar_t *line;      /* one short line under it                */
    const wchar_t *ready;     /* said again on the Ready page           */
    int            picture;   /* PIC_*                                  */
} style_info;

/* THE NAMES ARE THE WEBSITE'S, word for word: she reads them there
 * first, and the installer must say the same thing. "One thing at a
 * time" is Tiles because that is Tiles' own name (shells/tiles.shell),
 * which AurOS's Settings shows her later; Big and simple is Rail. */
static const style_info STYLES[] = {
  { "taskbar", "sandstone", L"Like Windows",
    L"A bar along the bottom, just like now.",
    L"It will look like Windows: a bar along the bottom.", PIC_WINDOWS },
  { "dock",    "sandstone", L"Like a Mac",
    L"Your programs in a row along the bottom.",
    L"It will look like a Mac: your programs in a row along the bottom.", PIC_MAC },
  { "rail",    "sandstone", L"Big and simple",
    L"Big buttons. Nothing ever gets hidden.",
    L"It will be big and simple: big buttons, and nothing ever gets hidden.", PIC_SIMPLE },
  { "tiles",   "sandstone", L"One thing at a time",
    L"Each program fills the whole screen.",
    L"It will show one thing at a time: each program fills the whole screen.", PIC_ONE },
};
#define N_STYLES ((int)(sizeof STYLES / sizeof STYLES[0]))
/* What she knows: Windows. */
#define STYLE_DEFAULT 0

/* themes/sandstone.theme ("Light", the theme every card installs) and
 * the footer's fixed colours (src/aurshell/foot.c), for the pictures. */
#define LT_PAPER    0xF6F2E9u
#define LT_SOFT     0xECE6DAu
#define LT_CARD     0xFCFAF5u
#define LT_RULE     0x6E675Cu
#define LT_TEXT     0xBDB6AAu   /* a line of text, drawn as a stub     */
#define LT_INK      0x17181Au
#define LT_GO       0x0B5D4Fu
#define FOOT_BG     0x14171Bu
#define FOOT_INK    0x9BA3ABu

/* ── Pages ────────────────────────────────────────────────────────── */
/* Three screens before the progress screen, and nothing to type on any
 * of them (docs/handoff/SESSION-2026-10-23.md, D3). BLOCKED is not a
 * step: it is what the first screen becomes when something stops it. */
typedef enum {
    PAGE_WELCOME = 0,     /* what AurOS is, and the check, together */
    PAGE_BLOCKED,         /* something stops it, and what we do      */
    PAGE_STYLE,           /* how would you like it to look?          */
    PAGE_READY,           /* what happens, and how long              */
    PAGE_PROGRESS,        /* one bar, then Restart now               */
    PAGE_COUNT
} page_id;

static const struct { const wchar_t *label; page_id first; } RAIL[] = {
    { L"Check this computer", PAGE_WELCOME  },
    { L"Pick a look",         PAGE_STYLE    },
    { L"Ready",               PAGE_READY    },
    { L"Install",             PAGE_PROGRESS },
};
#define N_RAIL ((int)(sizeof RAIL / sizeof RAIL[0]))

static int rail_index_for(page_id p)
{
    if (p == PAGE_BLOCKED) return 0;            /* stopped at the check */
    for (int i = N_RAIL - 1; i >= 0; i--)
        if (p >= RAIL[i].first) return i;
    return 0;
}

/* ── Global UI state ──────────────────────────────────────────────── */
static HWND     g_hwnd;
static int      g_dpi = 96;
static int      g_cw, g_ch;            /* client size, device px         */
static page_id  g_page = PAGE_WELCOME;

/* back-buffer: a 32bpp top-down DIB we both rasterise into by hand and
 * let GDI draw ClearType text into. */
static HDC      g_mdc;
static HBITMAP  g_mbmp, g_moldbmp;
static uint32_t *g_px;
static int      g_mw, g_mh;

static HFONT g_f_title, g_f_h3, g_f_body, g_f_bodyb, g_f_small, g_f_smallb, g_f_btn;

static RECT  g_clip;                   /* clip for hand-rasterised pixels */

/* input */
static POINT g_mouse = { -1, -1 };
static int   g_mouse_down = 0;
static int   g_press_idx = -1;
static int   g_focus = -1;             /* index into g_w[]                */
static int   g_focus_ring = 0;         /* draw rings only after Tab/arrow */

/* scrolling */
static int   g_scroll[PAGE_COUNT + 1];  /* one per page, and one for the Terms/Licenses pane */
static int   g_pane;                   /* 0 none, 1 the Terms, 2 the Licenses (read in the window) */
static int   scr_i(void) { return g_pane ? (int)PAGE_COUNT : (int)g_page; }
static int   g_content_h, g_view_h;    /* measured during paint           */

/* preflight */
static pf_report    g_report;          /* the last finished check (UI thread only) */
static pf_report    g_pf_new;          /* the one being run (worker only)          */
static volatile LONG g_pf_state;       /* 0 idle · 1 running · 2 has a result      */
static int          g_pf_valid;        /* g_report holds a real result     */
static int          g_checking;        /* a check is running right now    */
static int          g_ticks;           /* animation clock (60 ms)          */
static page_id      g_check_next = PAGE_STYLE;   /* where a clean check goes */

/* her one answer */
static int   g_sel_style = STYLE_DEFAULT;
/* D10: her Wi-Fi comes across unless she unticks it. */
static int   g_bring_wifi = 1;

/* Language, keyboard and time zone: what Windows already uses, read at
 * start-up (detect_defaults) and handed over without a question. The
 * lists keep a few more so --navtest can prove the values map. */
#define OPT_MAX 10
static int   g_sel_lang = 0, g_sel_kbd = 0, g_sel_tz = 0;
static wchar_t g_langs[OPT_MAX][96]; static char g_lang_v[OPT_MAX][64]; static int g_n_langs;
static wchar_t g_kbds [OPT_MAX][96]; static char g_kbd_v [OPT_MAX][64]; static int g_n_kbds;
static wchar_t g_tzs  [OPT_MAX][96]; static char g_tz_v  [OPT_MAX][96]; static int g_n_tzs;

/* detected-from-Windows defaults, filled in at startup */
static wchar_t g_det_lang[96], g_det_kbd[96], g_det_tz[128];

/* dev harness only; never set by anything the user can click */
static int   g_shot_mode = 0;
static int   g_nav_mode  = 0;
static int   g_selftest  = 0;

/* SET ONLY BY "Restart now". Nothing starts the install but her press:
 * the engine stops short of arming the next start (ab_choice.arm_later)
 * and ab_arm() runs in do_restart(). "Restart later" is a window that
 * waits (minimised); closing it takes everything back. So there is no
 * state in which the computer is armed and nobody is looking
 * (docs/research/people-walkthrough-2026-10.md, C-08). */
static int   g_restarting = 0;
static int   g_nt_restarts, g_nt_waits;       /* --navtest's counters */

/* ═══════════════════════════════════════════════════════════════════
 *  Small helpers
 * ═══════════════════════════════════════════════════════════════════ */
static int S(int v) { return MulDiv(v, g_dpi, 96); }

static void a2w(const char *a, wchar_t *w, int cch)
{
    if (!MultiByteToWideChar(CP_ACP, 0, a, -1, w, cch)) { w[0] = 0; }
}

static float clampf(float v, float lo, float hi)
{ return v < lo ? lo : (v > hi ? hi : v); }

/* ═══════════════════════════════════════════════════════════════════
 *  Hand-rasterised, anti-aliased primitives.
 *
 *  GDI cannot anti-alias a rounded rectangle, and RoundRect's stair-step
 *  corners are the single most obvious "this is a 1998 dialog" tell. We
 *  own the DIB bits, so shapes are drawn with a signed-distance field and
 *  per-pixel coverage; text is still GDI/ClearType on the same surface.
 *
 *  GdiFlush() before touching bits: GDI batches, and a batched TextOut
 *  landing after our writes would otherwise appear on top of them.
 * ═══════════════════════════════════════════════════════════════════ */
static inline void blend_px(int x, int y, uint32_t col, float a)
{
    if (a <= 0.003f) return;
    if (x < g_clip.left || x >= g_clip.right ||
        y < g_clip.top  || y >= g_clip.bottom) return;
    uint32_t *p = &g_px[(size_t)y * (size_t)g_mw + (size_t)x];
    if (a >= 0.997f) { *p = col; return; }
    uint32_t d = *p;
    int ia = (int)(a * 256.0f);
    int r = (int)((d >> 16) & 0xFF), g = (int)((d >> 8) & 0xFF), b = (int)(d & 0xFF);
    r += ((int)((col >> 16) & 0xFF) - r) * ia >> 8;
    g += ((int)((col >>  8) & 0xFF) - g) * ia >> 8;
    b += ((int)( col        & 0xFF) - b) * ia >> 8;
    *p = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* signed distance to a rounded rectangle; negative inside */
static inline float sd_rr(float px, float py, float cx, float cy,
                          float hw, float hh, float r)
{
    float dx = fabsf(px - cx) - (hw - r);
    float dy = fabsf(py - cy) - (hh - r);
    float ax = dx > 0.f ? dx : 0.f;
    float ay = dy > 0.f ? dy : 0.f;
    float m  = dx > dy ? dx : dy;
    if (m > 0.f) m = 0.f;
    return sqrtf(ax * ax + ay * ay) + m - r;
}

/* One horizontal run of constant coverage. The rounded-rect fill below
 * routes everything that is not within a corner through here, which is
 * what keeps a full-window repaint cheap on the ten-year-old PCs this
 * installer exists for. */
static inline void span(int py, int xa, int xb, uint32_t col, float a)
{
    if (py < g_clip.top || py >= g_clip.bottom) return;
    if (xa < g_clip.left)  xa = g_clip.left;
    if (xb > g_clip.right) xb = g_clip.right;
    if (xa >= xb || a <= 0.003f) return;
    if (a >= 0.997f) {
        uint32_t *p = &g_px[(size_t)py * (size_t)g_mw + (size_t)xa];
        for (int i = xb - xa; i > 0; i--) *p++ = col;
        return;
    }
    for (int x = xa; x < xb; x++) blend_px(x, py, col, a);
}

static void fill_rr(float x, float y, float w, float h, float r,
                    uint32_t col, float alpha)
{
    if (w <= 0.f || h <= 0.f) return;
    GdiFlush();
    float hw = w * 0.5f, hh = h * 0.5f;
    if (r > hw) r = hw;
    if (r > hh) r = hh;
    float cx = x + hw, cy = y + hh;

    int x0 = (int)floorf(x) - 1, x1 = (int)ceilf(x + w) + 1;
    int y0 = (int)floorf(y) - 1, y1 = (int)ceilf(y + h) + 1;
    if (x0 < g_clip.left) x0 = g_clip.left;
    if (y0 < g_clip.top)  y0 = g_clip.top;
    if (x1 > g_clip.right)  x1 = g_clip.right;
    if (y1 > g_clip.bottom) y1 = g_clip.bottom;

    int m  = (int)ceilf(r) + 1;
    int mx0 = (int)ceilf(x)  + m, mx1 = (int)floorf(x + w) - m;
    int my0 = (int)ceilf(y)  + m, my1 = (int)floorf(y + h) - m;

    for (int py = y0; py < y1; py++) {
        float fy = (float)py + 0.5f;
        if (py >= my0 && py < my1) {
            /* straight part: coverage depends on x alone */
            for (int px = x0; px < x1 && px < mx0; px++)
                blend_px(px, py, col,
                         clampf(0.5f - (fabsf((float)px + 0.5f - cx) - hw), 0.f, 1.f) * alpha);
            span(py, mx0 > x0 ? mx0 : x0, mx1 < x1 ? mx1 : x1, col, alpha);
            for (int px = mx1 > x0 ? mx1 : x0; px < x1; px++)
                blend_px(px, py, col,
                         clampf(0.5f - (fabsf((float)px + 0.5f - cx) - hw), 0.f, 1.f) * alpha);
        } else {
            /* corner band: SDF at the ends, flat coverage in between */
            for (int px = x0; px < x1 && px < mx0; px++)
                blend_px(px, py, col,
                         clampf(0.5f - sd_rr((float)px + 0.5f, fy, cx, cy, hw, hh, r),
                                0.f, 1.f) * alpha);
            float covy = clampf(0.5f - (fabsf(fy - cy) - hh), 0.f, 1.f);
            span(py, mx0 > x0 ? mx0 : x0, mx1 < x1 ? mx1 : x1, col, covy * alpha);
            for (int px = mx1 > x0 ? mx1 : x0; px < x1; px++)
                blend_px(px, py, col,
                         clampf(0.5f - sd_rr((float)px + 0.5f, fy, cx, cy, hw, hh, r),
                                0.f, 1.f) * alpha);
        }
    }
}

/* Only the four border strips are visited, never the interior. */
static void stroke_rr(float x, float y, float w, float h, float r,
                      float t, uint32_t col, float alpha)
{
    if (w <= 0.f || h <= 0.f) return;
    GdiFlush();
    float hw = w * 0.5f, hh = h * 0.5f;
    if (r > hw) r = hw;
    if (r > hh) r = hh;
    float cx = x + hw, cy = y + hh, ht = t * 0.5f;

    int x0 = (int)floorf(x - t) - 1, x1 = (int)ceilf(x + w + t) + 1;
    int y0 = (int)floorf(y - t) - 1, y1 = (int)ceilf(y + h + t) + 1;
    if (x0 < g_clip.left) x0 = g_clip.left;
    if (y0 < g_clip.top)  y0 = g_clip.top;
    if (x1 > g_clip.right)  x1 = g_clip.right;
    if (y1 > g_clip.bottom) y1 = g_clip.bottom;

    int band = (int)ceilf(r + t) + 2;
    int ytop = (int)ceilf(y) + band, ybot = (int)floorf(y + h) - band;
    int xl   = (int)ceilf(x) + band, xr   = (int)floorf(x + w) - band;

    for (int py = y0; py < y1; py++) {
        float fy = (float)py + 0.5f;
        int edge_row = (py < ytop || py >= ybot);
        if (edge_row) {
            for (int px = x0; px < x1; px++) {
                float d = fabsf(sd_rr((float)px + 0.5f, fy, cx, cy, hw, hh, r)) - ht;
                blend_px(px, py, col, clampf(0.5f - d, 0.f, 1.f) * alpha);
            }
        } else {
            for (int px = x0; px < x1; px++) {
                if (px >= xl && px < xr) { px = xr - 1; continue; }
                float d = fabsf(fabsf((float)px + 0.5f - cx) - hw) - ht;
                blend_px(px, py, col, clampf(0.5f - d, 0.f, 1.f) * alpha);
            }
        }
    }
}

/* The AurOS mark. Drawn, not typed: a missing glyph in a substituted
 * font would put a hollow box where the brand should be. */
static void fill_diamond(float cx, float cy, float r, uint32_t col, float a)
{
    GdiFlush();
    int x0 = (int)floorf(cx - r) - 1, x1 = (int)ceilf(cx + r) + 1;
    int y0 = (int)floorf(cy - r) - 1, y1 = (int)ceilf(cy + r) + 1;
    if (x0 < g_clip.left) x0 = g_clip.left;
    if (y0 < g_clip.top)  y0 = g_clip.top;
    if (x1 > g_clip.right)  x1 = g_clip.right;
    if (y1 > g_clip.bottom) y1 = g_clip.bottom;
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            float d = (fabsf((float)px + 0.5f - cx) + fabsf((float)py + 0.5f - cy) - r)
                      * 0.70711f;
            blend_px(px, py, col, clampf(0.5f - d, 0.f, 1.f) * a);
        }
}

static void fill_circle(float cx, float cy, float r, uint32_t col, float a)
{ fill_rr(cx - r, cy - r, r * 2.f, r * 2.f, r, col, a); }

static void stroke_circle(float cx, float cy, float r, float t, uint32_t col, float a)
{ stroke_rr(cx - r, cy - r, r * 2.f, r * 2.f, r, t, col, a); }

static void aa_line(float ax, float ay, float bx, float by,
                    float t, uint32_t col, float alpha)
{
    GdiFlush();
    float ht = t * 0.5f;
    int x0 = (int)floorf((ax < bx ? ax : bx) - t) - 1;
    int x1 = (int)ceilf ((ax > bx ? ax : bx) + t) + 1;
    int y0 = (int)floorf((ay < by ? ay : by) - t) - 1;
    int y1 = (int)ceilf ((ay > by ? ay : by) + t) + 1;
    if (x0 < g_clip.left) x0 = g_clip.left;
    if (y0 < g_clip.top)  y0 = g_clip.top;
    if (x1 > g_clip.right)  x1 = g_clip.right;
    if (y1 > g_clip.bottom) y1 = g_clip.bottom;
    float vx = bx - ax, vy = by - ay;
    float len2 = vx * vx + vy * vy;
    if (len2 < 1e-6f) len2 = 1e-6f;
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            float wx = (float)px + 0.5f - ax, wy = (float)py + 0.5f - ay;
            float s  = clampf((wx * vx + wy * vy) / len2, 0.f, 1.f);
            float dx = wx - vx * s, dy = wy - vy * s;
            float d  = sqrtf(dx * dx + dy * dy) - ht;
            blend_px(px, py, col, clampf(0.5f - d, 0.f, 1.f) * alpha);
        }
}

/* a check mark, drawn rather than typed: no font dependency */
static void glyph_check(float cx, float cy, float s, uint32_t col, float a)
{
    aa_line(cx - s * 0.46f, cy + s * 0.02f, cx - s * 0.10f, cy + s * 0.38f,
            s * 0.20f, col, a);
    aa_line(cx - s * 0.12f, cy + s * 0.38f, cx + s * 0.48f, cy - s * 0.36f,
            s * 0.20f, col, a);
}
static void glyph_bang(float cx, float cy, float s, uint32_t col, float a)
{
    aa_line(cx, cy - s * 0.40f, cx, cy + s * 0.10f, s * 0.19f, col, a);
    fill_circle(cx, cy + s * 0.38f, s * 0.11f, col, a);
}

/* The ground: flat paper. It was two glowing ribbons of teal and violet
 * (docs/DESIGN.md: no gradient as decoration). */
static void ground(void)
{
    GdiFlush();
    for (int i = 0; i < g_mw * g_mh; i++) g_px[i] = C_BG;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Text
 * ═══════════════════════════════════════════════════════════════════ */
/* --navtest ASKS EVERY LETTER IT DRAWS whether the face has it: a glyph the
 * face lacks is drawn from another font, in another shape (the typed ›
 * was one). g_missing counts them, g_missing_cp is the first. */
static int g_missing, g_missing_cp, g_face_ok_early;
static void glyph_audit(HFONT f, const wchar_t *s, int n)
{
    if (!g_nav_mode || !g_face_ok_early || !s) return;
    if (n < 0) n = (int)wcslen(s);
    WORD idx[512];
    if (n > 512) n = 512;
    SelectObject(g_mdc, f);
    if (GetGlyphIndicesW(g_mdc, s, n, idx, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR) return;
    for (int i = 0; i < n; i++)
        if (idx[i] == 0xFFFF && s[i] > 32) { if (!g_missing++) g_missing_cp = s[i]; }
}

/* ONE TYPEFACE, TWO WEIGHTS, FIVE SIZES (fonts_make). Each size has its
 * own line height: a paragraph at one and a half times its size, which
 * older eyes read far more easily than the font's own spacing (about
 * 1.2, all DrawText offers; WCAG 1.4.12), and a title held tighter. So
 * paragraphs are broken into lines here, a whole word at a time. */
static struct { HFONT *f; int px, weight; float lh; int line, tm; int display; } TYPE[] = {
    { &g_f_title,  32, FW_NORMAL, 1.25f, 0, 0, 1 },  /* the page's one title, 800: 32/40 */
    { &g_f_h3,     22, FW_BOLD,   1.36f, 0, 0, 0 },  /* a look's name, a status: 22/30 */
    { &g_f_body,   20, FW_NORMAL, 1.50f, 0, 0, 0 },  /* what she reads: 20/30 */
    { &g_f_bodyb,  20, FW_BOLD,   1.50f, 0, 0, 0 },
    { &g_f_small,  18, FW_NORMAL, 1.45f, 0, 0, 0 },  /* the floor: 18/26      */
    { &g_f_smallb, 18, FW_BOLD,   1.45f, 0, 0, 0 },
    { &g_f_btn,    20, FW_BOLD,   1.20f, 0, 0, 0 },  /* a button's label      */
};
#define N_TYPE ((int)(sizeof TYPE / sizeof TYPE[0]))

static int type_of(HFONT f)
{
    for (int i = 0; i < N_TYPE; i++) if (*TYPE[i].f == f) return i;
    return 2;
}

/* Lays out (draw=0) or draws a paragraph; returns its height. Explicit
 * newlines are kept; a word longer than the line stands alone. */
static int para(const wchar_t *s, HFONT f, uint32_t col, int x, int y, int w, int draw)
{
    int t = type_of(f), lh = TYPE[t].line, off = (lh - TYPE[t].tm) / 2;
    SelectObject(g_mdc, f);
    if (draw) { SetTextColor(g_mdc, CR(col)); SetBkMode(g_mdc, TRANSPARENT); }
    int n = 0;
    const wchar_t *p = s;
    while (*p) {
        const wchar_t *end = p, *fit = NULL;
        for (;;) {
            const wchar_t *q = end;
            while (*q && *q != L' ' && *q != L'\n') q++;
            SIZE sz = { 0, 0 };
            GetTextExtentPoint32W(g_mdc, p, (int)(q - p), &sz);
            if (fit && sz.cx > w) break;
            fit = q;
            if (!*q || *q == L'\n') break;
            end = q + 1;
        }
        glyph_audit(f, p, (int)(fit - p));
        if (draw) TextOutW(g_mdc, x, y + n * lh + off, p, (int)(fit - p));
        n++;
        p = fit;
        if (*p == L' ' || *p == L'\n') p++;
    }
    return n * lh;
}

static int text_h(const wchar_t *s, HFONT f, int w, UINT flags)
{
    if (flags & DT_WORDBREAK) return para(s, f, 0, 0, 0, w, 0);
    RECT r = { 0, 0, w, 1 << 20 };
    SelectObject(g_mdc, f);
    DrawTextW(g_mdc, s, -1, &r, flags | DT_CALCRECT | DT_NOPREFIX);
    return r.bottom - r.top;
}

static int text_w(const wchar_t *s, HFONT f)
{
    RECT r = { 0, 0, 1 << 20, 1 << 20 };
    SelectObject(g_mdc, f);
    DrawTextW(g_mdc, s, -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    return r.right - r.left;
}

/* returns the height consumed */
static int text_draw(const wchar_t *s, HFONT f, uint32_t col,
                     int x, int y, int w, UINT flags)
{
    if (flags & DT_WORDBREAK) return para(s, f, col, x, y, w, 1);
    RECT r = { x, y, x + w, y + (1 << 20) };
    glyph_audit(f, s, -1);
    SelectObject(g_mdc, f);
    SetTextColor(g_mdc, CR(col));
    SetBkMode(g_mdc, TRANSPARENT);
    DrawTextW(g_mdc, s, -1, &r, flags | DT_NOPREFIX);
    return TYPE[type_of(f)].line;
}

static void text_in(const wchar_t *s, HFONT f, uint32_t col, RECT box, UINT flags)
{
    glyph_audit(f, s, -1);
    SelectObject(g_mdc, f);
    SetTextColor(g_mdc, CR(col));
    SetBkMode(g_mdc, TRANSPARENT);
    DrawTextW(g_mdc, s, -1, &box, flags | DT_NOPREFIX);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Immediate-mode widgets.
 *
 *  Every interactive thing registers its rectangle during paint. Hit
 *  testing and Tab order then read that list, so there is exactly one
 *  description of where things are and no chance of the clickable area
 *  drifting from the drawn one.
 *
 *  THREE KINDS, AND NO MORE. There used to be a text box (for typing
 *  AGREE), check boxes that had to be ticked to go on (a backup she
 *  probably did not have) and chips. A button and a picture card are
 *  what this installer asks her to use; a toggle is there only to say
 *  no to something already said yes to for her (D10, her Wi-Fi), and it
 *  never holds the way on. --navtest fails on anything else.
 * ═══════════════════════════════════════════════════════════════════ */
typedef enum { W_BUTTON, W_CARD, W_TOGGLE } wkind;

typedef struct {
    int id; wkind kind; RECT r; int enabled;
    const wchar_t *name;      /* what a screen reader says it is (static text) */
} widget;

static widget g_w[96];
static int    g_nw;

enum {
    ID_NONE = 0,
    ID_PRIMARY, ID_BACK, ID_QUIT, ID_LATER, ID_WIFI, ID_HELPER, ID_COPY, ID_STOP, ID_LEAVE, ID_REPORT,
    ID_TERMS, ID_LICENSES,
    ID_STYLE = 500
};

static int w_add(int id, wkind k, int x, int y, int w, int h, int enabled)
{
    if (g_nw >= (int)(sizeof g_w / sizeof g_w[0])) return -1;
    widget *p = &g_w[g_nw];
    p->id = id; p->kind = k; p->enabled = enabled; p->name = NULL;
    p->r.left = x; p->r.top = y; p->r.right = x + w; p->r.bottom = y + h;
    return g_nw++;
}

static int w_hot(int idx)
{
    if (idx < 0) return 0;
    const widget *p = &g_w[idx];
    return p->enabled && g_mouse.x >= p->r.left && g_mouse.x < p->r.right &&
           g_mouse.y >= p->r.top && g_mouse.y < p->r.bottom;
}
static int w_focused(int idx) { return idx >= 0 && idx == g_focus; }

static int w_find_at(int x, int y)
{
    for (int i = g_nw - 1; i >= 0; i--) {
        const widget *p = &g_w[i];
        if (p->enabled && x >= p->r.left && x < p->r.right &&
            y >= p->r.top && y < p->r.bottom) return i;
    }
    return -1;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Components
 * ═══════════════════════════════════════════════════════════════════ */
/* The keyboard's ring: 3 px of ink, 4 px out, its element's radius + 3. */
static void focus_ring(RECT r, int pad, int radius)
{
    if (!g_focus_ring) return;
    stroke_rr((float)(r.left - pad), (float)(r.top - pad),
              (float)(r.right - r.left + pad * 2), (float)(r.bottom - r.top + pad * 2),
              (float)radius, (float)S(3), C_INK, 1.f);
}

/* WHAT CAN BE PRESSED LOOKS PRESSABLE: the green fill, or a 2 px rule,
 * a verb on it, radius 8. Under the pointer only the border changes,
 * to 2 px of ink. `icon` 1 puts a check mark before the label (Install
 * AurOS, so it never looks like the Next that stood in its place). */
static int draw_button(int id, const wchar_t *label, int x, int y, int w, int h,
                       int primary, int icon)
{
    int idx = w_add(id, W_BUTTON, x, y, w, h, 1);
    if (idx >= 0) g_w[idx].name = label;
    int hot = w_hot(idx);
    int down = hot && g_mouse_down && g_press_idx == idx;
    float r = (float)S(8);
    uint32_t ink = primary ? C_ON_ACCENT : C_INK;
    if (primary) {
        fill_rr((float)x, (float)y, (float)w, (float)h, r, down ? C_ACCENT_DOWN : C_ACCENT, 1.f);
        if (hot) stroke_rr((float)x, (float)y, (float)w, (float)h, r, (float)S(2), C_INK, 1.f);
    } else {
        fill_rr((float)x, (float)y, (float)w, (float)h, r, down ? C_SOFT : C_SURFACE, 1.f);
        stroke_rr((float)x, (float)y, (float)w, (float)h, r, (float)S(2), hot ? C_INK : C_RULE, 1.f);
    }
    RECT box = { x, y, x + w, y + h };
    if (icon) {
        int iw = S(30), tw = text_w(label, g_f_btn);
        int sx = x + (w - iw - tw) / 2;
        glyph_check((float)(sx + S(10)), (float)(y + h / 2), (float)S(20), ink, 1.f);
        box.left = sx + iw;
        text_in(label, g_f_btn, ink, box, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
    } else {
        text_in(label, g_f_btn, ink, box, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
    if (w_focused(idx)) focus_ring((RECT){ x, y, x + w, y + h }, S(4), S(11));
    return idx;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Preflight plumbing
 *
 *  The worker fills its own report and the UI thread copies it over
 *  when it is finished, so the page on screen never reads a report
 *  that is half written -- which it used to, for the second check
 *  behind "Start installing".
 * ═══════════════════════════════════════════════════════════════════ */
/* ═══════════════════════════════════════════════════════════════════
 *  WILL HER HARDWARE WORK? (stream HW: src/aurbridge/hwinv_win.h)
 *
 *  The inventory is read on the worker beside the check, and only what the
 *  pages need is kept: each verdict (a finished sentence of hers, from
 *  hw/seeds.json) and the printers' names. One thing stops her, because
 *  it ends in a computer with no way to the internet: a Wi-Fi card AurOS
 *  cannot drive and no network cable socket (`blocks`). It is a stop
 *  like any other (the id "no-internet", fix_win.c), so the gate, the
 *  stop page and the re-check every few seconds all treat it the same.
 *  With a cable socket the same card is only a line under "Good to know"
 *  on the first page. A camera, a printer, a scanner that will not work
 *  yet are ONE honest line on Ready, never a stop.
 * ═══════════════════════════════════════════════════════════════════ */
typedef struct {
    int        n;
    int        blocks;
    hw_verdict v[HW_MAX_VERDICTS];
    char       printer[HW_MAX_PRINTERS][96];
    int        n_printers;
} hw_view;
static hw_view g_hw, g_hw_new, g_hw_bg;

/* The finished inventory, kept small; and the stop it makes, put in the
 * check's own report so that nothing downstream has to know about it. */
static void hw_view_from(hw_view *out, const hw_inv *inv)
{
    memset(out, 0, sizeof *out);
    out->n = inv->n_verdicts < HW_MAX_VERDICTS ? inv->n_verdicts : HW_MAX_VERDICTS;
    memcpy(out->v, inv->verdicts, (size_t)out->n * sizeof out->v[0]);
    out->n_printers = inv->n_printers < HW_MAX_PRINTERS ? inv->n_printers : HW_MAX_PRINTERS;
    for (int i = 0; i < out->n_printers; i++)
        snprintf(out->printer[i], sizeof out->printer[i], "%s", inv->printers[i].name);
    out->blocks = hwinv_blocks(inv);
}

static void hw_add_stop(pf_report *r, const hw_view *v)
{
    if (!v->blocks || r->n >= PF_MAX_RESULTS) return;
    for (int i = 0; i < r->n; i++) if (!strcmp(r->results[i].id, "no-internet")) return;
    pf_result *x = &r->results[r->n++];
    memset(x, 0, sizeof *x);
    snprintf(x->id, sizeof x->id, "no-internet");
    snprintf(x->risk, sizeof x->risk, "R13");
    snprintf(x->title, sizeof x->title, "AurOS would have no internet on this computer");
    for (int i = 0; i < v->n; i++)
        if (v->v[i].blocks) { snprintf(x->detail, sizeof x->detail, "%s", v->v[i].says); break; }
    x->sev = PF_BLOCK;
    r->n_block++;
}

/* Read this computer's hardware and judge it (one to three seconds). */
static void hw_collect(hw_view *out, pf_report *pf)
{
    memset(out, 0, sizeof *out);
    hw_inv *inv = hwinv_new();
    if (!inv) return;
    hwinv_scan(inv, pf);
    hw_view_from(out, inv);
    hwinv_free(inv);
    hw_add_stop(pf, out);
}

/* The sentence of the verdict that blocks, as hers */
static const char *hw_block_says(void)
{
    for (int i = 0; i < g_hw.n; i++) if (g_hw.v[i].blocks) return g_hw.v[i].says;
    return "";
}

/* WHAT WON'T WORK YET, in one line, for Ready: the names of the things, and
 * only what is not a stop. 0 when there is nothing to say. */
static int hw_ready_line(wchar_t *out, int cch)
{
    wchar_t wont[320] = L"", slow[96] = L"", unk[200] = L"";
    int nw = 0, nu = 0;
    for (int i = 0; i < g_hw.n; i++) {
        const hw_verdict *v = &g_hw.v[i];
        if (v->kind == HW_K_WIFI || v->kind == HW_K_ETHERNET || v->blocks) continue;   /* the first page's */
        const wchar_t *name = NULL;
        wchar_t pn[130];
        switch (v->kind) {
        case HW_K_CAMERA:      name = L"your camera"; break;
        case HW_K_BLUETOOTH:   name = L"your Bluetooth"; break;
        case HW_K_AUDIO:       name = L"your sound"; break;
        case HW_K_SCANNER:     name = L"your scanner"; break;
        case HW_K_FINGERPRINT: name = L"your fingerprint reader"; break;
        case HW_K_GPU:         name = NULL; break;
        default:
            name = L"your printer";
            if (v->printer >= 0 && g_hw.n_printers > 1) {
                wchar_t w[96];
                MultiByteToWideChar(CP_UTF8, 0, g_hw.printer[v->printer], -1, w, 96);
                w[95] = 0;
                _snwprintf(pn, 129, v->state == HW_V_PARTIAL ? L"the scanner of %ls" : L"%ls", w);
                pn[129] = 0;
                name = pn;
            } else if (v->state == HW_V_PARTIAL) name = L"your printer\u2019s scanner";
            break;
        }
        if (v->kind == HW_K_GPU) {
            if (v->state == HW_V_PARTIAL) wcscpy(slow, L" Games and 3D programs will be slow.");
            continue;
        }
        if (!name) continue;
        wchar_t *list = NULL; int *cnt = NULL;
        if (v->state == HW_V_WONT || v->state == HW_V_PARTIAL) { list = wont; cnt = &nw; }
        else if (v->state == HW_V_UNKNOWN) { list = unk; cnt = &nu; }
        if (!list) continue;
        if (*cnt) wcsncat(list, L", ", 300 - wcslen(list) - 1);
        wcsncat(list, name, 300 - wcslen(list) - 1);
        (*cnt)++;
    }
    out[0] = 0;
    if (nw) _snwprintf(out, (size_t)cch, L"Won\u2019t work yet: %ls.%ls", wont, slow);
    else if (slow[0]) _snwprintf(out, (size_t)cch, L"%ls", slow + 1);
    if (nu) {
        size_t k = wcslen(out);
        _snwprintf(out + k, (size_t)cch - k, L"%ls We can\u2019t tell about: %ls.", k ? L"" : L"", unk);
    }
    out[cch - 1] = 0;
    return out[0] != 0;
}

/* The Wi-Fi and cable verdicts that are not a stop, for "Good to know" on
 * the first page. Returns how many were put in `lines`. */
static int hw_internet_notes(const wchar_t **lines, int max, wchar_t bufs[][330])
{
    int n = 0;
    for (int i = 0; i < g_hw.n && n < max; i++) {
        const hw_verdict *v = &g_hw.v[i];
        if ((v->kind != HW_K_WIFI && v->kind != HW_K_ETHERNET) || v->blocks) continue;
        if (v->state == HW_V_WORKS) continue;
        MultiByteToWideChar(CP_UTF8, 0, v->says, -1, bufs[n], 329);
        bufs[n][329] = 0;
        lines[n] = bufs[n];
        n++;
    }
    return n;
}

/* WHAT THE SPACE FIX WOULD FREE, measured by the worker that ran the
 * check (fix_temp_bytes reads, never deletes), so the stop page says the
 * size before she presses. Less than TEMP_WORTH is not a fix at all. */
static uint64_t g_temp_new, g_temp_bytes, g_bg_temp;
#define TEMP_WORTH (100ULL * 1000 * 1000)

static int pf_blocks(const pf_report *r, const char *id)
{
    for (int i = 0; i < r->n; i++)
        if (r->results[i].sev == PF_BLOCK && !strcmp(r->results[i].id, id)) return 1;
    return 0;
}

static DWORD WINAPI pf_worker(LPVOID p)
{
    (void)p;
    pf_run(&g_pf_new);                 /* read-only, by contract */
    g_temp_new = pf_blocks(&g_pf_new, "insufficient-space") ? fix_temp_bytes() : 0;
    hw_collect(&g_hw_new, &g_pf_new);  /* and her hardware, read-only too */
    InterlockedExchange(&g_pf_state, 2);
    return 0;
}

static void pf_start(void)
{
    if (InterlockedCompareExchange(&g_pf_state, 1, 0) != 0) return;
    g_checking = 1;
    memset(&g_pf_new, 0, sizeof g_pf_new);
    HANDLE h = CreateThread(NULL, 0, pf_worker, NULL, 0, NULL);
    if (h) CloseHandle(h);
    else { pf_run(&g_pf_new); InterlockedExchange(&g_pf_state, 2); }
}

/* 1 when a check has just finished and g_report now holds it. */
static int pf_poll(void)
{
    if (InterlockedCompareExchange(&g_pf_state, 0, 2) != 2) return 0;
    memcpy(&g_report, &g_pf_new, sizeof g_report);
    memcpy(&g_hw, &g_hw_new, sizeof g_hw);
    g_temp_bytes = g_temp_new;
    g_pf_valid = 1;
    g_checking = 0;
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Fixing what the check found, instead of describing it
 * ═══════════════════════════════════════════════════════════════════
 *
 * The page a stopped check lands on used to be a report: every problem in
 * a paragraph, a "WHAT TO DO" box, and a Check again button. It is a list
 * now -- the problem and what happens about it, one line each (fix.h) --
 * with one button that fixes everything a program can fix, and a check
 * that runs again by itself every few seconds, so that plugging in the
 * charger or taking out a USB stick is all somebody has to do. */
enum { FS_NONE = 0, FS_RUNNING, FS_DONE, FS_FAILED };
#define FIX_MAX 24
static struct { char id[48]; int state; int after; wchar_t why[200]; } g_fx[FIX_MAX];
static int           g_fx_n;
static volatile LONG g_fix_busy;       /* the fix worker is running        */
static pf_report     g_bg_report;      /* the quiet re-check's own copy    */
static int           g_bg_hw;          /* ... and it reads her hardware again */
static volatile LONG g_bg_state;       /* 0 idle, 1 running, 2 has a result */
static int           g_bg_ticks;
static int           g_resumed;        /* opened again after our restart  */

static int fx_index(const char *id)
{
    for (int i = 0; i < g_fx_n; i++) if (!strcmp(g_fx[i].id, id)) return i;
    if (g_fx_n >= FIX_MAX) return -1;
    snprintf(g_fx[g_fx_n].id, sizeof g_fx[0].id, "%s", id);
    g_fx[g_fx_n].state = FS_NONE;
    g_fx[g_fx_n].after = 0;
    g_fx[g_fx_n].why[0] = 0;
    return g_fx_n++;
}

static fix_kind kind_of(const pf_result *r)
{
    const fix_info *f = fix_lookup(r->id);
    if (!f) return FIX_UNKNOWN;
    /* Next to nothing to delete: there is no fix, only what she can move. */
    if (!strcmp(r->id, "insufficient-space") && g_temp_bytes < TEMP_WORTH) return FIX_WAIT;
    /* A restart is offered once. Still there after it: nothing we can do. */
    if (f->kind == FIX_RESTART && g_resumed) return FIX_CANNOT;
    return f->kind;
}

/* What the one button on the stop page would do: 1 if anything shown is
 * something a program can fix, 2 if one of those needs a restart. */
static int fixable_now(void)
{
    int any = 0;
    for (int i = 0; i < g_report.n; i++) {
        if (g_report.results[i].sev != PF_BLOCK) continue;
        fix_kind k = kind_of(&g_report.results[i]);
        /* Something nothing can fix is there too: fixing the rest (and
         * restarting somebody's PC for it) would change nothing. */
        if (k == FIX_CANNOT) return 0;
        if (k == FIX_RESTART) any = 2;
        if (k == FIX_AUTO && !any) any = 1;
    }
    return any;
}

static DWORD WINAPI fix_worker(LPVOID p)
{
    (void)p;
    /* The ids were copied out on the UI thread before this started; the
     * report itself is only ever touched there. */
    int restart = 0;
    for (int i = 0; i < g_fx_n; i++) {
        const fix_info *f = fix_lookup(g_fx[i].id);
        if (!f) continue;
        if (f->kind == FIX_RESTART) { restart = !g_resumed; continue; }
        if (f->kind != FIX_AUTO) continue;
        g_fx[i].state = FS_RUNNING;
        char why[200] = "";
        if (fix_run(g_fx[i].id, why, sizeof why) == 0) g_fx[i].state = FS_DONE;
        else {
            MultiByteToWideChar(CP_UTF8, 0, why, -1, g_fx[i].why, 199);
            g_fx[i].state = FS_FAILED;
        }
    }
    /* LAST, AND ONCE: every other fix is done first so the restart is the
     * only one, and the installer opens again after it (fix.h). */
    if (restart) {
        char why[200] = "";
        for (int i = 0; i < g_fx_n; i++) {
            const fix_info *f = fix_lookup(g_fx[i].id);
            if (f && f->kind == FIX_RESTART) g_fx[i].state = FS_RUNNING;
        }
        g_restarting = 1;
        if (fix_restart_and_continue(why, sizeof why) != 0) {
            g_restarting = 0;
            for (int i = 0; i < g_fx_n; i++) {
                const fix_info *f = fix_lookup(g_fx[i].id);
                if (f && f->kind == FIX_RESTART) {
                    MultiByteToWideChar(CP_UTF8, 0, why, -1, g_fx[i].why, 199);
                    g_fx[i].state = FS_FAILED;
                }
            }
        }
    }
    InterlockedExchange(&g_fix_busy, 0);
    g_bg_ticks = 1000;                 /* check again straight away */
    return 0;
}

static int any_cannot(void)
{
    for (int i = 0; i < g_report.n; i++)
        if (g_report.results[i].sev == PF_BLOCK && kind_of(&g_report.results[i]) == FIX_CANNOT)
            return 1;
    return 0;
}

static void fix_start(void)
{
    if (!fixable_now()) return;
    if (g_selftest || g_nav_mode || g_shot_mode) return;   /* harnesses never fix */
    if (InterlockedCompareExchange(&g_fix_busy, 1, 0) != 0) return;
    for (int i = 0; i < g_report.n; i++) {
        if (g_report.results[i].sev != PF_BLOCK) continue;
        fix_kind k = kind_of(&g_report.results[i]);
        if (k != FIX_AUTO && k != FIX_RESTART) continue;
        int j = fx_index(g_report.results[i].id);
        if (j >= 0) { g_fx[j].state = FS_NONE; g_fx[j].after = 0; g_fx[j].why[0] = 0; }
    }
    HANDLE h = CreateThread(NULL, 0, fix_worker, NULL, 0, NULL);
    if (h) CloseHandle(h);
    else InterlockedExchange(&g_fix_busy, 0);
}

static DWORD WINAPI bg_worker(LPVOID p)
{
    (void)p;
    pf_run(&g_bg_report);              /* read-only, by contract */
    g_bg_temp = pf_blocks(&g_bg_report, "insufficient-space") ? fix_temp_bytes() : 0;
    /* her hardware again only while it is what stops her: she may have
     * plugged in the USB Wi-Fi stick */
    if (g_bg_hw) hw_collect(&g_hw_bg, &g_bg_report); else g_hw_bg = g_hw;
    InterlockedExchange(&g_bg_state, 2);
    return 0;
}

/* The quiet re-check, from tick(): every few seconds while the stop page
 * is up and nothing is being fixed. When it comes back clean the page
 * moves on by itself. */
static int bg_recheck_tick(void)
{
    if (InterlockedCompareExchange(&g_bg_state, 0, 2) == 2) {
        memcpy(&g_report, &g_bg_report, sizeof g_report);
        memcpy(&g_hw, &g_hw_bg, sizeof g_hw);
        g_temp_bytes = g_bg_temp;
        g_pf_valid = 1;
        g_bg_ticks = 0;
        /* a fix that has run, and been checked since */
        for (int i = 0; i < g_fx_n; i++) if (g_fx[i].state == FS_DONE) g_fx[i].after = 1;
        return 1;
    }
    if (g_fix_busy || g_bg_state != 0) return 0;
    if (++g_bg_ticks < 50) return 0;   /* about three seconds */
    g_bg_ticks = 0;
    if (InterlockedCompareExchange(&g_bg_state, 1, 0) != 0) return 0;
    memset(&g_bg_report, 0, sizeof g_bg_report);
    g_bg_hw = g_hw.blocks;
    HANDLE h = CreateThread(NULL, 0, bg_worker, NULL, 0, NULL);
    if (h) CloseHandle(h);
    else InterlockedExchange(&g_bg_state, 0);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Navigation — the safety gate.
 *
 *  This function, not the absence of a button, is what makes a blocked
 *  machine unreachable past the first page.
 * ═══════════════════════════════════════════════════════════════════ */
static int style_ok(void)
{
    return g_sel_style >= 0 && g_sel_style < N_STYLES;
}

static int nav_allowed(page_id p)
{
    switch (p) {
    case PAGE_WELCOME:
        return 1;
    case PAGE_BLOCKED:
        return g_pf_valid && g_report.n_block > 0;
    default:
        break;
    }
    /* Hard gate. Nothing past the first page exists for a machine we
     * have refused, and nothing exists at all until preflight has run.
     * R3/#3 in the red-team register: refusing is the product outcome. */
    if (!g_pf_valid || !pf_is_go(&g_report)) return 0;

    switch (p) {
    case PAGE_STYLE:    return 1;
    case PAGE_READY:    return style_ok();
    /* Reachable only through check_advance(), after a check run when
     * she pressed "Install AurOS" has come back clean. */
    case PAGE_PROGRESS: return nav_allowed(PAGE_READY) && !g_checking;
    default:            return 0;
    }
}

static int g_max_page;

/* ONE PRESS IS ONE PRESS (docs/DESIGN-DIRECTION.md, "Input"). Her habit
 * is to double-click, and "Next" on the look page sits where "Install
 * AurOS" sits on Ready: a double-click on Next started the install
 * without her ever seeing the page that says what it costs. So every
 * press is ignored for 700 ms after the page -- or what its big button
 * does -- changes, and a second press on the same thing within 500 ms
 * is ignored too: by mouse, keyboard and screen reader alike. */
#define PRESS_SETTLE_MS 700
#define PRESS_REPEAT_MS 500
static ULONGLONG g_changed_at, g_pressed_at;
static int       g_pressed_id;
static ULONGLONG g_nt_clock = 100000;  /* --navtest's clock               */
static int       g_nt_auto = 1;        /* it moves on 1 s before each press */

static ULONGLONG now_ms(void) { return g_nav_mode ? g_nt_clock : GetTickCount64(); }
static const wchar_t *g_early;
static void page_changed(void) { g_changed_at = now_ms(); g_early = NULL; }

static int press_allowed(int id)
{
    if (g_nav_mode && g_nt_auto) g_nt_clock += 1000;
    ULONGLONG t = now_ms();
    if (t - g_changed_at < PRESS_SETTLE_MS) return 0;
    if (id == g_pressed_id && t - g_pressed_at < PRESS_REPEAT_MS) return 0;
    g_pressed_id = id;
    g_pressed_at = t;
    return 1;
}

static void goto_page(page_id p)
{
    if (!nav_allowed(p)) {
        /* Should be unreachable: the UI never offers a disallowed move.
         * Kept because "unreachable" is a claim, and this is the proof. */
        OutputDebugStringA("aurbridge: navigation refused\n");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    g_page = p;
    g_pane = 0;
    page_changed();
    if ((int)p > g_max_page) g_max_page = (int)p;
    g_focus = -1;
    g_focus_ring = 0;
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

/* ═══════════════════════════════════════════════════════════════════
 *  The phases, as the engine runs them.
 *
 *  The progress page no longer lists these -- it shows one bar and one
 *  sentence (page_progress) -- but install_begin() and install_tick()
 *  keep their state per phase, so the table stays, and its words stay
 *  true: three of them used to promise things nothing did ("about 5 GB",
 *  "sleep is tested", "only now does anything become permanent").
 * ═══════════════════════════════════════════════════════════════════ */
typedef enum { PH_PENDING = 0, PH_RUNNING, PH_DONE, PH_LATER, PH_FAILED } ph_state;

static const struct {
    const wchar_t *name;
    const wchar_t *desc;
    int after_restart;
} PHASES[] = {
 { L"Look at this computer once more",
   L"Every safety check runs again, right before we start.", 0 },
 { L"Write down what was chosen",
   L"Her answers, for after the restart.", 0 },
 { L"Get AurOS",
   L"AurOS is downloaded and checked, piece by piece.", 0 },
 { L"Add AurOS to the start-up menu",
   L"AurOS is offered once, at the next start.", 0 },
 { L"Make room",
   L"Room is made beside Windows. Her files stay where they are.", 1 },
 { L"Copy AurOS onto the drive",
   L"AurOS is written into the new space, then checked.", 1 },
 { L"Try it out on this computer",
   L"AurOS checks it can see the drive before anything is final.", 1 },
 { L"Your first look",
   L"AurOS asks whether everything works.", 1 },
 { L"Bring your files across",
   L"Documents and photos can be copied over.", 1 },
};
#define N_PHASES ((int)(sizeof PHASES / sizeof PHASES[0]))

static int   g_ph[N_PHASES];
static int   g_ph_cur = -1;
static float g_ph_prog;
static int   g_install_running, g_install_finished;
/* Everything before the restart is done and the one-shot is armed:
 * closing the window now would take it all back (install_cancel), so
 * WM_CLOSE asks first. */
static int   g_install_armed;

#define LOG_MAX 80
static wchar_t g_log[LOG_MAX][200];
static int     g_log_n;


/* ═══════════════════════════════════════════════════════════════════
 *  Driving the real thing
 *
 *  What used to be here was six functions that logged what a real
 *  phase WOULD do and returned success, with install_tick() advancing
 *  a fake progress bar over them on a timer. The engine is in phases.c
 *  now, so this is what is left: a worker thread, two callbacks, and
 *  a rule.
 *
 *  THE RULE: NOTHING IN THIS FILE OPENS A HANDLE. Not a disk, not a
 *  file, not a firmware variable. Everything that touches the world is
 *  behind plat.h, and the reason is not tidiness -- it is that the
 *  window procedure runs on the UI thread and anything that blocks it
 *  stops the window painting, so the temptation to "just read this one
 *  thing here" produces a program that appears to have crashed in the
 *  middle of preparing somebody's disk.
 *
 *  So: ab_run() on its own thread, and it reports back through say()
 *  and progress(), which do nothing but copy into the log and set two
 *  numbers under a lock. The UI reads them on its timer as before.
 * ═══════════════════════════════════════════════════════════════════ */

static CRITICAL_SECTION g_ab_lock;
static int    g_ab_lock_ready;
static HANDLE g_ab_thread;
static ab_machine g_ab_machine;
static ab_choice  g_ab_choice;
static char   g_ab_why[400];
static volatile LONG g_ab_state;   /* 0 idle, 1 running, 2 done, 3 failed */
/* SHE ASKED TO STOP while it was working. A step is never cut off half
 * way: that could leave the one-time start-up setting armed, or
 * BitLocker suspended, with nothing left to take them back (atlas
 * review, finding 12). The engine is let finish the step it is on, then
 * everything is taken back (ab_abort) and the window closes by itself.
 * g_ab_gate is who got there first: 0 nothing yet, 1 the engine has
 * started, 2 she stopped it before it did (it then never starts). */
static volatile LONG g_stop_wanted;
static volatile LONG g_ab_gate;
static int    g_aborts;            /* ab_abort calls, for --navtest       */
/* THE QUESTION WHEN SHE CLOSES THE WINDOW, drawn in the window (the stock
 * message box was a different program's words, in a different look):
 * 0 none, 1 stop setting up?, 2 close with AurOS ready? Esc cancels it. */
static int    g_confirm;
static int    leave_confirmed(void);
static int    g_ab_phase;          /* which ab_phase is running           */
static int    g_ab_pct;

static void ab_lock_init(void)
{ if (!g_ab_lock_ready) { InitializeCriticalSection(&g_ab_lock); g_ab_lock_ready = 1; } }

/* The engine talks in UTF-8 because it also builds for a host that is
 * not Windows. The log is wide. This is the only place they meet. */
static void ab_say_cb(const char *line, void *ud)
{
    (void)ud;
    wchar_t w[400];
    MultiByteToWideChar(CP_UTF8, 0, line, -1, w, 399);
    w[399] = 0;
    EnterCriticalSection(&g_ab_lock);
    if (g_log_n < LOG_MAX) wcscpy(g_log[g_log_n++], w);
    else {
        memmove(g_log[0], g_log[1], sizeof g_log - sizeof g_log[0]);
        wcscpy(g_log[LOG_MAX - 1], w);
    }
    /* A line beginning with the box-drawing rule is the engine saying
     * it has moved on to the next phase. Matching on that rather than
     * counting lines: the number of lines a phase prints is not a
     * contract and changing it should not silently desynchronise the
     * tick marks from the work. */
    if (line[0] == (char)0xE2)              /* the UTF-8 lead byte of ── */
        g_ab_phase++;
    LeaveCriticalSection(&g_ab_lock);
}

static void ab_prog_cb(int pct, void *ud)
{ (void)ud; EnterCriticalSection(&g_ab_lock); g_ab_pct = pct;
  LeaveCriticalSection(&g_ab_lock); }

/* Where this program is, so that the image and the staging environment
 * beside it can be named without asking the user where she put them. */
/* BOTH CONVERSIONS ARE CHECKED, and neither used to be.
 *
 * GetModuleFileNameW truncates silently when the path does not fit and
 * reports the buffer size, so a deep install directory produced a
 * DIFFERENT directory that looked plausible. WideCharToMultiByte
 * returns 0 and writes nothing when the UTF-8 form does not fit --
 * 260 wide characters of CJK can need 780 bytes and `dir` holds 519 --
 * leaving uninitialised stack to be formatted into a path the
 * installer then tries to read five gigabytes from.
 *
 * An empty `out` is the honest answer to both, and every caller
 * already treats it as "not there". */
static void beside_me(char *out, size_t n, const char *leaf)
{
    out[0] = 0;
    wchar_t w[MAX_PATH];
    DWORD k = GetModuleFileNameW(NULL, w, MAX_PATH);
    if (k == 0 || k >= MAX_PATH) return;    /* 0, or truncated */
    w[k] = 0;
    for (DWORD i = k; i > 0; i--)
        if (w[i - 1] == L'\\' || w[i - 1] == L'/') { w[i] = 0; break; }
    char dir[MAX_PATH * 4];
    int dn = WideCharToMultiByte(CP_UTF8, 0, w, -1, dir, sizeof dir, NULL, NULL);
    if (dn <= 0) return;
    dir[sizeof dir - 1] = 0;
    if (_snprintf(out, n - 1, "%s%s", dir, leaf) < 0) { out[0] = 0; return; }
    out[n - 1] = 0;
}

static HANDLE g_dl_thread;             /* defined with download_begin() */

/* Wait for the early download to end. The legacy path does it before the
 * engine starts; the fast path from inside phase 2, after Windows has made
 * the room -- the shrink hides behind the download. */
static void wait_early_download(void *ud)
{
    (void)ud;
    if (g_dl_thread) {
        WaitForSingleObject(g_dl_thread, INFINITE);
        CloseHandle(g_dl_thread);
        g_dl_thread = NULL;
    }
}

static DWORD WINAPI ab_worker(LPVOID p)
{
    (void)p;
    /* WAIT FOR THE EARLY DOWNLOAD FIRST, so nothing writes \AurOS\ while
     * ab_run's ensure_image() reads or rewrites it. This wait is on the
     * worker thread, not the UI thread, so the window keeps painting;
     * and because the download ran while she read the pages, it is
     * usually already finished and this returns at once. A failed early
     * fetch is not fatal -- ab_run's own ensure_image() retries it.
     *
     * THE FAST PATH DOES NOT WAIT HERE: it checks the machine and has
     * Windows shrink C: while the download is still running, and waits
     * only when it needs the pieces (ab_choice.pieces_wait). */
    if (g_ab_choice.fast_path) {
        g_ab_choice.pieces_wait = wait_early_download;
        g_ab_choice.pieces_wait_ud = NULL;
    } else {
        wait_early_download(NULL);
    }
    if (InterlockedCompareExchange(&g_ab_gate, 1, 0) != 0) {
        _snprintf(g_ab_why, sizeof g_ab_why - 1, "it was stopped.");
        InterlockedExchange(&g_ab_state, 3);
        return 0;
    }
    int rc = ab_run(AB_HANDOFF, &g_ab_choice, &g_report, &g_ab_machine,
                    ab_say_cb, ab_prog_cb, NULL, g_ab_why, sizeof g_ab_why);
    InterlockedExchange(&g_ab_state, rc == 0 ? 2 : 3);
    return 0;
}

/* WHAT SHE CHOSE, as values the installed system can act on: the look
 * from the one question she is asked (STYLES[]), and what Windows
 * already knows about her language, keyboard and time zone
 * (detect_defaults) -- asked of Windows, not of her. Its own function so
 * that --navtest can read back exactly what the engine is handed. */
/* D10, D11: OFFERED ONLY WHEN THE ENGINE CAN DO IT. phases.h defines
 * AB_CHOICE_BRING_WIFI and AB_CHOICE_BITLOCKER_PAUSE when ab_choice has
 * bring_wifi and bitlocker_pause and the engine acts on them; until then
 * the box is not drawn and the drive lock is not mentioned, because a
 * promise nothing keeps is worse than no promise. */
static int wifi_offered(void)
{
#ifdef AB_CHOICE_BRING_WIFI
    return g_report.wifi_connected;
#else
    return 0;
#endif
}

static int bitlocker_paused_here(void)
{
#ifdef AB_CHOICE_BITLOCKER_PAUSE
    for (int i = 0; i < g_report.n_volumes; i++)
        if (g_report.volumes[i].bitlocker == 1) return 1;
#endif
    return 0;
}

static void choices_from_page(ab_choice *c)
{
    int s = style_ok() ? g_sel_style : STYLE_DEFAULT;
    _snprintf(c->shell_archetype, sizeof c->shell_archetype - 1, "%s", STYLES[s].shell);
    _snprintf(c->theme, sizeof c->theme - 1, "%s", STYLES[s].theme);
    _snprintf(c->language, sizeof c->language - 1, "%s", g_lang_v[g_sel_lang]);
    _snprintf(c->keyboard, sizeof c->keyboard - 1, "%s", g_kbd_v[g_sel_kbd]);
    _snprintf(c->timezone, sizeof c->timezone - 1, "%s", g_tz_v[g_sel_tz]);
#ifdef AB_CHOICE_BRING_WIFI
    c->bring_wifi = wifi_offered() && g_bring_wifi;
#endif
#ifdef AB_CHOICE_TERMS
    /* Install AurOS said "By pressing Install AurOS you agree to the AurOS
     * Terms"; the version she agreed to and when are written to the install
     * journal, on this computer only (LEGAL item 2). "v3" is the Terms'
     * version, docs/legal/TERMS.md line 3. */
    _snprintf(c->terms_version, sizeof c->terms_version - 1, "%s", "v3");
    c->terms_unix = (unsigned long long)time(NULL);
#endif
#ifdef AB_CHOICE_BITLOCKER_PAUSE
    c->bitlocker_pause = bitlocker_paused_here();
#endif
}

/* WHERE THE IMAGE IS, OR WILL BE -- the part of the choice that does
 * not depend on anything she picks on the pages, so it can be filled in
 * the moment the check passes and the early download (download_begin)
 * can start while she reads. Both install_begin() and download_begin()
 * fill their own ab_choice with this, so the two never share a struct
 * one writes while the other reads. */
static void choice_image_source(ab_choice *c)
{
    memset(c, 0, sizeof *c);
    _snprintf(c->profile, sizeof c->profile - 1, "%s", "desktop");
    /* NO MEMORY STICK. See page_backup() and docs/AURBRIDGE.md,
     * "Installing without a memory stick". */
    c->no_stick = 1;
    c->stick_serial[0] = 0;
#ifdef AB_CHOICE_FAST_PATH
    /* WINDOWS MAKES THE ROOM, AurOS is written into it while it downloads,
     * and the restart only checks and commits (decision D1). The engine
     * falls back to the old way by itself where Windows cannot make
     * enough room, and a BitLocker PC that cannot have the fast way is
     * refused before anything is changed. */
    c->fast_path = 1;
#endif
    /* WHERE THE IMAGE GOES: \AurOS\ on the drive Windows started from,
     * because that is the volume the staging environment will mount
     * after the restart -- the journal names that partition and no
     * other. An image already sitting beside the installer is moved
     * there rather than downloaded again. */
    {
        char sd[16] = "C:";
        DWORD k = GetEnvironmentVariableA("SystemDrive", sd, sizeof sd);
        if (k == 0 || k >= sizeof sd || sd[1] != ':') snprintf(sd, sizeof sd, "C:");
        sd[2] = 0;
        _snprintf(c->image_path, sizeof c->image_path - 1,
                  "%s\\AurOS\\auros-desktop.img", sd);
    }
    beside_me(c->image_alt_path, sizeof c->image_alt_path, "auros-desktop.img");
    c->pieces_text = AUROS_PIECES_TEXT;
    /* WHERE TO GET IT, BAKED IN WHEN THIS WAS BUILT. Empty in a
     * developer build, which then requires the image to be sitting
     * there already -- the arrangement this product had before it was
     * one file. */
    _snprintf(c->image_url, sizeof c->image_url - 1, "%s", AUROS_IMAGE_URL);
    _snprintf(c->image_sha256, sizeof c->image_sha256 - 1, "%s", AUROS_IMAGE_SHA256);
    c->image_expect = AUROS_IMAGE_BYTES;
    /* AND THE STAGING ENVIRONMENT IS INSIDE THIS FILE. Leaving these
     * empty is what says so: phases.c unpacks them from the
     * executable's own resources. */
    c->kernel_path[0] = 0;
    c->initrd_path[0] = 0;
}

/* ── the early download ──────────────────────────────────────────────
 *
 * The five-gigabyte download is the floor of the whole install, and it
 * used to start only after every page was read and the final button
 * pressed -- so the wait came AFTER the reading, not during it. The
 * 2026-10-23 audit put ~6 minutes of staring at a bar where it did not
 * need to be. So the moment the check passes, this starts fetching the
 * image into \AurOS\ in the background, keeping the machine awake, while
 * she reads. It writes nothing but that one file (and its pieces),
 * which is non-destructive and resumable -- closing the window leaves a
 * partial download behind that the next run simply continues.
 *
 * It has its OWN ab_choice and its OWN worker thread. install_begin()'s
 * worker waits for this one to finish before ab_run() touches the
 * image, so the two never write \AurOS\ at once; and if the early fetch
 * failed, ab_run()'s own ensure_image() retries it, so the early
 * download is an optimisation, never a dependency. */
/* g_dl_thread is declared above ab_worker, which waits on it. */
static ab_choice    g_dl_choice;
static volatile LONG g_dl_state;   /* 0 idle, 1 running, 2 done, 3 failed */
static int          g_dl_started;
static char         g_dl_why[400];

static DWORD WINAPI dl_worker(LPVOID p)
{
    (void)p;
    /* The same callbacks as the install: the lines and the percentage
     * land in the shared log, so when she reaches the progress page the
     * download is already reflected there. ab_worker does not run
     * concurrently with this -- it waits for this thread first. */
    int rc = ab_fetch_image(&g_dl_choice, ab_say_cb, ab_prog_cb, NULL,
                            g_dl_why, sizeof g_dl_why);
    InterlockedExchange(&g_dl_state, rc == 0 ? 2 : 3);
    return 0;
}

static void download_begin(void)
{
    if (g_dl_started) return;              /* once per run of the wizard */
    g_dl_started = 1;
    ab_lock_init();
    choice_image_source(&g_dl_choice);
    /* Nothing to fetch in a developer build with no address baked in;
     * the image is expected beside the installer, and ab_run handles
     * that. Only start the background fetch when there is somewhere to
     * fetch from. */
    if (!g_dl_choice.pieces_text[0] && !g_dl_choice.image_url[0]) return;
    plat_keep_awake();
    InterlockedExchange(&g_dl_state, 1);
    g_dl_thread = CreateThread(NULL, 0, dl_worker, NULL, 0, NULL);
    if (!g_dl_thread) { InterlockedExchange(&g_dl_state, 0); g_dl_started = 0; }
}

static void install_begin(void)
{
    ab_lock_init();
    g_log_n = 0;
    for (int i = 0; i < N_PHASES; i++)
        g_ph[i] = PHASES[i].after_restart ? PH_LATER : PH_PENDING;
    g_ph_cur = 0;
    g_ph_prog = 0.f;
    g_ab_phase = -1;
    g_ab_pct = 0;
    g_ab_why[0] = 0;
    g_install_running = 1;
    g_install_finished = 0;
    g_stop_wanted = 0;
    InterlockedExchange(&g_ab_gate, 0);

    choice_image_source(&g_ab_choice);
    choices_from_page(&g_ab_choice);
    /* SET BY A PERSON AND NOTHING ELSE. Reaching this function means
     * she has read the page that says what happens and pressed the
     * button on it; phases.c refuses without it, and that refusal is
     * the last line of defence if this screen is ever reorganised. */
    g_ab_choice.consent_given = 1;
    /* ... and the next start is armed only by her press on Restart now */
    g_ab_choice.arm_later = 1;

    /* The machine stays awake from here through the restart even if the
     * early download never started (a developer build, or she moved
     * faster than download_begin). Idempotent. */
    plat_keep_awake();

    InterlockedExchange(&g_ab_state, 1);
    g_ab_thread = CreateThread(NULL, 0, ab_worker, NULL, 0, NULL);
    if (!g_ab_thread) {
        InterlockedExchange(&g_ab_state, 3);
        _snprintf(g_ab_why, sizeof g_ab_why - 1,
                  "this computer would not start the installer.");
    }
    g_ph[0] = PH_RUNNING;
}

/* Driven from WM_TIMER so the window keeps painting. It reads what the
 * worker has published and never waits for it. */
static void install_tick(void)
{
    if (!g_install_running) return;
    LONG st = g_ab_state;
    int phase, pct;
    EnterCriticalSection(&g_ab_lock);
    phase = g_ab_phase; pct = g_ab_pct;
    LeaveCriticalSection(&g_ab_lock);

    if (phase < 0) phase = 0;
    if (phase >= AB_N) phase = AB_N - 1;
    for (int i = 0; i < phase && i < N_PHASES; i++)
        if (!PHASES[i].after_restart) g_ph[i] = PH_DONE;
    if (phase < N_PHASES && !PHASES[phase].after_restart)
        g_ph[phase] = PH_RUNNING;
    g_ph_cur = phase;
    g_ph_prog = pct / 100.f;

    if (st == 2) {
        for (int i = 0; i < N_PHASES; i++)
            if (!PHASES[i].after_restart) g_ph[i] = PH_DONE;
        g_ph_cur = -1;
        g_install_running = 0;
        g_install_finished = 1;
        g_install_armed = 1;
        plat_let_sleep();            /* ready and waiting: the computer may sleep (the
                                      * window comes forward when it wakes) */
    } else if (st == 3) {
        if (phase < N_PHASES) g_ph[phase] = PH_FAILED;
        g_ph_cur = -1;
        g_install_running = 0;
        g_install_finished = 1;
        wchar_t w[400];
        MultiByteToWideChar(CP_UTF8, 0,
                            g_ab_why[0] ? g_ab_why : "it stopped", -1, w, 399);
        w[399] = 0;
        EnterCriticalSection(&g_ab_lock);
        if (g_log_n < LOG_MAX) wcscpy(g_log[g_log_n++], w);
        LeaveCriticalSection(&g_ab_lock);
    }
}

/* If the window is closed before the restart, everything that was done
 * is undone -- which before the restart is all of it. */
static void install_cancel(void)
{
    if (g_ab_state == 1) return;      /* a phase is mid-write: see g_stop_wanted */
    g_aborts++;
    if (g_nav_mode || g_shot_mode) return;
    ab_abort(&g_ab_machine, ab_say_cb, NULL);
    /* Give the machine's sleep timers back: the run is over, whether it
     * got as far as the engine or only started the background download.
     * A partial download left in \AurOS\ is harmless and resumes. */
    plat_let_sleep();
}

/* SHE IS LEAVING WITHOUT INSTALLING: the download stops, and what it put
 * on the Windows drive (gigabytes, in \AurOS\) goes again. Only when the
 * window goes -- a retry in the same window keeps it. A download that
 * will not stop within half a minute keeps its files rather than having
 * them deleted under it. */
static void install_forget(void)
{
    ab_cancel_downloads();
    if (g_dl_thread) {
        if (WaitForSingleObject(g_dl_thread, 30000) != WAIT_OBJECT_0) return;
        CloseHandle(g_dl_thread);
        g_dl_thread = NULL;
    }
    if (g_ab_state == 1) return;          /* the engine is mid-step */
    ab_forget_image(g_ab_choice.image_path[0] ? &g_ab_choice : &g_dl_choice,
                    NULL, NULL);
}

/* ═══════════════════════════════════════════════════════════════════
 *  What Windows already knows about this user
 * ═══════════════════════════════════════════════════════════════════ */

static void opt_push(wchar_t (*list)[96], char (*vals)[64], int *n,
                     const wchar_t *label, const char *value)
{
    if (!label || !*label || *n >= OPT_MAX) return;
    for (int i = 0; i < *n; i++) if (!_wcsicmp(list[i], label)) return;
    wcsncpy(list[*n], label, 95); list[*n][95] = 0;
    snprintf(vals[*n], 64, "%s", value ? value : "");
    (*n)++;
}
/* The time-zone values are longer ("windows:" and a Windows key name),
 * so they get their own width. */
static void tz_push(const wchar_t *label, const char *value)
{
    if (!label || !*label || g_n_tzs >= OPT_MAX) return;
    for (int i = 0; i < g_n_tzs; i++) if (!_wcsicmp(g_tzs[i], label)) return;
    wcsncpy(g_tzs[g_n_tzs], label, 95); g_tzs[g_n_tzs][95] = 0;
    snprintf(g_tz_v[g_n_tzs], sizeof g_tz_v[0], "%s", value ? value : "");
    g_n_tzs++;
}

typedef DWORD (WINAPI *PFN_GDTZI)(PDYNAMIC_TIME_ZONE_INFORMATION);

static void detect_defaults(void)
{
    /* LANGUAGE: Windows' locale name, "en-GB", is a glibc one, en_GB,
     * with a hyphen for an underscore -- when it has that shape. A name
     * with a script in it ("zh-Hans-CN") keeps its label and gets no
     * value: first boot then leaves the language to Ferry, which reads
     * it out of Windows itself, rather than guessing. */
    wchar_t name[LOCALE_NAME_MAX_LENGTH];
    char det_lang_v[64] = "";
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH)) {
        if (!GetLocaleInfoEx(name, LOCALE_SLOCALIZEDDISPLAYNAME, g_det_lang, 96))
            wcsncpy(g_det_lang, name, 95);
        char a[LOCALE_NAME_MAX_LENGTH];
        if (WideCharToMultiByte(CP_UTF8, 0, name, -1, a, sizeof a, NULL, NULL) > 0) {
            size_t l = strlen(a);
            int ok = (l == 5 || l == 6) && a[l - 3] == '-';
            for (size_t i = 0; ok && i < l; i++) {
                char ch = a[i];
                if (i < l - 3) ok = ch >= 'a' && ch <= 'z';
                else if (i > l - 3) ok = ch >= 'A' && ch <= 'Z';
            }
            if (ok) {
                a[l - 3] = '_';
                snprintf(det_lang_v, sizeof det_lang_v, "%s.UTF-8", a);
            }
        }
    }
    /* KEYBOARD: the layout's id (KLID), which Ferry's table maps to an
     * XKB layout on the other side. The label is Windows' own name for
     * it, out of the registry. */
    wchar_t klid[KL_NAMELENGTH];
    char det_kbd_v[64] = "";
    if (GetKeyboardLayoutNameW(klid)) {
        wchar_t sub[200];
        _snwprintf(sub, 199,
            L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts\\%s", klid);
        sub[199] = 0;
        HKEY k;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ, &k) == ERROR_SUCCESS) {
            DWORD sz = sizeof g_det_kbd, type = 0;
            if (RegQueryValueExW(k, L"Layout Text", NULL, &type,
                                 (BYTE *)g_det_kbd, &sz) != ERROR_SUCCESS)
                g_det_kbd[0] = 0;
            RegCloseKey(k);
        }
        char a[16];
        if (WideCharToMultiByte(CP_UTF8, 0, klid, -1, a, sizeof a, NULL, NULL) > 0 &&
            strlen(a) == 8)
            snprintf(det_kbd_v, sizeof det_kbd_v, "klid:%s", a);
    }
    /* TIME ZONE: Windows' key name ("Pacific Standard Time"), which is
     * what the CLDR table on the other side is keyed by. It is in the
     * DYNAMIC structure, which is Vista and later, reached by
     * GetProcAddress so the binary still loads on anything older. */
    char det_tz_v[96] = "";
    {
        TIME_ZONE_INFORMATION tzi;
        memset(&tzi, 0, sizeof tzi);
        if (GetTimeZoneInformation(&tzi) != TIME_ZONE_ID_INVALID)
            wcsncpy(g_det_tz, tzi.StandardName, 127);
        PFN_GDTZI gd = (PFN_GDTZI)(void (*)(void))GetProcAddress(
            GetModuleHandleW(L"kernel32.dll"), "GetDynamicTimeZoneInformation");
        DYNAMIC_TIME_ZONE_INFORMATION d;
        memset(&d, 0, sizeof d);
        char a[160];
        if (gd && gd(&d) != TIME_ZONE_ID_INVALID && d.TimeZoneKeyName[0] &&
            WideCharToMultiByte(CP_UTF8, 0, d.TimeZoneKeyName, -1, a, sizeof a,
                                NULL, NULL) > 0)
            snprintf(det_tz_v, sizeof det_tz_v, "windows:%s", a);
    }

    /* The value Windows is already using goes first and is preselected:
     * the common case should need no clicks at all. */
    opt_push(g_langs, g_lang_v, &g_n_langs, g_det_lang, det_lang_v);
    opt_push(g_langs, g_lang_v, &g_n_langs, L"English (United States)", "en_US.UTF-8");
    opt_push(g_langs, g_lang_v, &g_n_langs, L"English (United Kingdom)", "en_GB.UTF-8");
    opt_push(g_langs, g_lang_v, &g_n_langs, L"Español", "es_ES.UTF-8");
    opt_push(g_langs, g_lang_v, &g_n_langs, L"Français", "fr_FR.UTF-8");
    opt_push(g_langs, g_lang_v, &g_n_langs, L"Deutsch", "de_DE.UTF-8");
    opt_push(g_langs, g_lang_v, &g_n_langs, L"Português (Brasil)", "pt_BR.UTF-8");
    opt_push(g_langs, g_lang_v, &g_n_langs, L"Italiano", "it_IT.UTF-8");
    opt_push(g_langs, g_lang_v, &g_n_langs, L"Polski", "pl_PL.UTF-8");

    opt_push(g_kbds, g_kbd_v, &g_n_kbds, g_det_kbd, det_kbd_v);
    opt_push(g_kbds, g_kbd_v, &g_n_kbds, L"US", "xkb:us");
    opt_push(g_kbds, g_kbd_v, &g_n_kbds, L"United Kingdom", "xkb:gb");
    opt_push(g_kbds, g_kbd_v, &g_n_kbds, L"Spanish", "xkb:es");
    opt_push(g_kbds, g_kbd_v, &g_n_kbds, L"French (AZERTY)", "xkb:fr");
    opt_push(g_kbds, g_kbd_v, &g_n_kbds, L"German (QWERTZ)", "xkb:de");
    opt_push(g_kbds, g_kbd_v, &g_n_kbds, L"Portuguese (Brazil)", "xkb:br");

    tz_push(g_det_tz, det_tz_v);
    tz_push(L"London", "iana:Europe/London");
    tz_push(L"Paris, Berlin, Madrid, Rome", "iana:Europe/Paris");
    tz_push(L"New York (Eastern)", "iana:America/New_York");
    tz_push(L"Chicago (Central)", "iana:America/Chicago");
    tz_push(L"Los Angeles (Pacific)", "iana:America/Los_Angeles");

    if (!g_n_langs) opt_push(g_langs, g_lang_v, &g_n_langs, L"English (United States)", "en_US.UTF-8");
    if (!g_n_kbds)  opt_push(g_kbds, g_kbd_v, &g_n_kbds, L"US", "xkb:us");
    if (!g_n_tzs)   tz_push(L"London", "iana:Europe/London");
}

/* ═══════════════════════════════════════════════════════════════════
 *  Layout
 *
 *  docs/DESIGN-DIRECTION.md §5 B-1: the steps in a flat soft sidebar,
 *  280 px, on the left; the page on flat paper to the window's edge; the
 *  buttons at the bottom right, their right edge the page's right edge.
 *  Below 1100 px of width the sidebar folds into one line at the top:
 *  "AurOS Setup · Step 2 of 4". No card floating on a backdrop.
 *
 *  Sized for the screens she has: a 1366x768 laptop gives this window
 *  a client area of about 1120x690, and so does a 1920x1080 one at
 *  150%; a 1024x600 netbook about 1024x530, and a 1366x768 laptop at
 *  150% about 910x465. Every page fits each without scrolling
 *  (--navtest checks); only the Secure Boot sketch is allowed to scroll,
 *  and the buttons never do.
 * ═══════════════════════════════════════════════════════════════════ */
/* A window under 500 px tall (150% on a 1366x768 laptop): the title is
 * 28 and the text 18, the floor, so a page still fits without scrolling. */
static int g_dense;
static void fonts_make(void);

static RECT g_card, g_body, g_foot;
static int  g_side;                    /* the sidebar's width; 0 folded  */
static int  g_short;                   /* a netbook-height window       */
static int  g_inset;

static int agree_shown(void);
static int agree_h(void);
static void layout(void)
{
    int lw = MulDiv(g_cw, 96, g_dpi), lh = MulDiv(g_ch, 96, g_dpi);
    g_short = lh < 600;
    if ((lh < 500) != g_dense) { g_dense = lh < 500; fonts_make(); }
    g_side  = lw >= 1100 ? S(280) : 0;
    g_inset = g_short || lw < 1100 ? S(24) : S(32);
    SetRect(&g_card, g_side, 0, g_cw, g_ch);

    g_foot.left   = g_side + g_inset;
    g_foot.right  = g_cw - g_inset;
    g_foot.bottom = g_ch - g_inset;
    g_foot.top    = g_foot.bottom - S(56);

    g_body.left   = g_side + g_inset;
    g_body.right  = g_cw - g_inset;
    if (g_body.right < g_body.left + S(200)) g_body.right = g_body.left + S(200);
    g_body.top    = g_inset + (g_side ? S(8) : S(26) + S(g_short ? 12 : 16));
    g_body.bottom = g_foot.top - S(g_short ? 16 : 24);
    /* THE AGREEMENT stands above Install AurOS (TERMS 4); its height is
     * what the sentence and the way to read the Terms need at this width */
    if (agree_shown()) g_body.bottom -= agree_h() - S(12);   /* the page's own 24 px of air is spent on it */
    if (g_body.bottom < g_body.top + S(120)) g_body.bottom = g_body.top + S(120);
}

static void set_clip(int l, int t, int r, int b)
{
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r > g_mw) r = g_mw;
    if (b > g_mh) b = g_mh;
    g_clip.left = l; g_clip.top = t; g_clip.right = r; g_clip.bottom = b;
    HRGN rgn = CreateRectRgn(l, t, r, b);
    SelectClipRgn(g_mdc, rgn);
    DeleteObject(rgn);
}
static void clip_reset(void) { set_clip(0, 0, g_mw, g_mh); }

/* ═══════════════════════════════════════════════════════════════════
 *  Shared page furniture
 * ═══════════════════════════════════════════════════════════════════ */

/* The wordmark: the mark, then "AurOS" bold and "Setup" regular. The
 * mark is drawn (fill_diamond), never typed, its height the capitals'. */
static int draw_wordmark(int x, int y, int h)
{
    int cap = TYPE[type_of(g_f_h3)].px * 70 / 100;
    fill_diamond((float)x + (float)S(cap) * 0.5f, (float)y + (float)h * 0.5f,
                 (float)S(cap) * 0.5f, C_ACCENT, 1.f);
    int tx = x + S(cap) + S(8);
    RECT b = { tx, y, tx + S(400), y + h };
    text_in(L"AurOS", g_f_h3, C_INK, b, DT_SINGLELINE | DT_VCENTER);
    tx += text_w(L"AurOS ", g_f_h3);
    RECT c = { tx, y, tx + S(400), y + h };
    text_in(L"Setup", g_f_body, C_INK, c, DT_SINGLELINE | DT_VCENTER);
    return tx + text_w(L"Setup", g_f_body);
}

/* The steps: done ones with a check mark, this one in green and bold,
 * the rest as rings. On the stop page "Check this computer" is this one. */
static void draw_steps(void)
{
    int cur = rail_index_for(g_page);
    if (!g_side) {
        int y = g_inset, h = S(26);
        int ex = draw_wordmark(g_body.left, y, h);
        /* not on the install page: it counts the install's own steps */
        if (g_page != PAGE_PROGRESS) {
            wchar_t st[48];
            _snwprintf(st, 47, L"Step %d of %d", cur + 1, N_RAIL);
            st[47] = 0;
            RECT b = { ex + S(24), y, g_body.right, y + h };
            text_in(st, g_f_small, C_INK2, b, DT_SINGLELINE | DT_VCENTER);
        }
        return;
    }
    fill_rr(0.f, 0.f, (float)g_side, (float)g_ch, 0.f, C_SOFT, 1.f);
    int x = S(32);
    draw_wordmark(x, S(32), S(30));
    int sy = S(120), step = S(60);
    for (int i = 0; i < N_RAIL; i++) {
        float cx = (float)(x + S(12)), cy = (float)(sy + i * step + S(15));
        if (i < N_RAIL - 1)
            aa_line(cx, cy + (float)S(14), cx, cy + (float)step - (float)S(14), (float)S(2),
                    C_RULE, 1.f);
        RECT b = { x + S(40), sy + i * step, g_side - S(16), sy + i * step + S(30) };
        if (i < cur) {
            stroke_circle(cx, cy, (float)S(11), (float)S(2), C_RULE, 1.f);
            glyph_check(cx, cy, (float)S(13), C_INK2, 1.f);
            text_in(RAIL[i].label, g_f_body, C_INK2, b, DT_SINGLELINE | DT_VCENTER);
        } else if (i == cur) {
            fill_circle(cx, cy, (float)S(12), C_ACCENT, 1.f);
            fill_circle(cx, cy, (float)S(4), C_ON_ACCENT, 1.f);
            text_in(RAIL[i].label, g_f_bodyb, C_INK, b, DT_SINGLELINE | DT_VCENTER);
        } else {
            stroke_circle(cx, cy, (float)S(11), (float)S(2), C_RULE, 1.f);
            text_in(RAIL[i].label, g_f_body, C_INK2, b, DT_SINGLELINE | DT_VCENTER);
        }
    }
}

/* WORKING, WITHOUT ANYTHING SPINNING: three segments, one lit at a time,
 * stepping every 600 ms (DESIGN-DIRECTION.md, motion: colour only). */
static void stepper(int x, int y, int w, int h)
{
    int seg = (w - S(8)) / 3;
    int lit = (int)((g_ticks * 60 / 600) % 3);
    for (int i = 0; i < 3; i++)
        fill_rr((float)(x + i * (seg + S(4))), (float)y, (float)seg, (float)h, (float)h * 0.5f,
                i == lit ? C_ACCENT : C_SOFT, 1.f);
}

/* Working, done, something she needs to do, or something we will do. */
enum { ST_WORKING = 0, ST_DONE, ST_TODO, ST_PENDING };
static void status_icon(float cx, float cy, float r, int st)
{
    if (st == ST_PENDING) {
        stroke_circle(cx, cy, r - (float)S(1), (float)S(2), C_RULE, 1.f);
    } else if (st == ST_WORKING) {
        stepper((int)(cx - r), (int)(cy - (float)S(3)), (int)(r * 2.f), S(6));
    } else if (st == ST_DONE) {
        fill_circle(cx, cy, r, C_ACCENT, 1.f);
        glyph_check(cx, cy, r * 1.15f, C_ON_ACCENT, 1.f);
    } else {
        fill_circle(cx, cy, r, C_WARN, 1.f);
        glyph_bang(cx, cy, r * 1.15f, C_ON_ACCENT, 1.f);
    }
}

/* WHAT WENT WRONG, IN ONE OF FOUR SENTENCES (DESIGN-DIRECTION.md B-6).
 * The engine's own words name files, drives and error numbers; they go
 * to the log, and she reads which of four things happened to her. */
static const wchar_t *plain_failure(const wchar_t *why)
{
    wchar_t lo[400];
    int i = 0;
    for (; why && why[i] && i < 399; i++) lo[i] = (wchar_t)towlower(why[i]);
    lo[i] = 0;
    OutputDebugStringW(why ? why : L"");
    static const wchar_t *const net[] = { L"download", L"internet", L"connect", L"network",
                                          L"server", L"piece", L"online", NULL };
    static const wchar_t *const room[] = { L"space", L"room", L"full", NULL };
    static const wchar_t *const win[] = { L"windows would not", L"windows wouldn", L"denied",
                                          L"permission", L"shrink", L"in use", NULL };
    for (int k = 0; net[k]; k++) if (wcsstr(lo, net[k]))
        return L"The internet stopped partway. Check you’re online, then press Try again.";
    for (int k = 0; win[k]; k++) if (wcsstr(lo, win[k]))
        return L"Windows wouldn’t let us make room. Restart Windows, then try again.";
    for (int k = 0; room[k]; k++) if (wcsstr(lo, room[k]))
        return L"This computer ran out of room. Press Try again.";
    return L"Something went wrong on our side. Nothing was changed.";
}

static void glyph_arc(float cx, float cy, float r, float a0, float a1,
                      float t, uint32_t c, float al)
{
    float px = 0.f, py = 0.f;
    int n = (int)(fabsf(a1 - a0) * 6.f) + 4;
    for (int i = 0; i <= n; i++) {
        float a = a0 + (a1 - a0) * (float)i / (float)n;
        float x = cx + cosf(a) * r, y = cy + sinf(a) * r;
        if (i) aa_line(px, py, x, y, t, c, al);
        px = x; py = y;
    }
}
static void glyph_undo(float cx, float cy, float s, uint32_t c, float a)
{
    glyph_arc(cx, cy, s * 0.42f, 2.6f, 2.6f + 5.0f, s * 0.09f, c, a);
    float ex = cx + cosf(2.6f) * s * 0.42f, ey = cy + sinf(2.6f) * s * 0.42f;
    aa_line(ex, ey, ex + s * 0.22f, ey - s * 0.14f, s * 0.09f, c, a);
    aa_line(ex, ey, ex + s * 0.08f, ey + s * 0.24f, s * 0.09f, c, a);
}
static void glyph_clock(float cx, float cy, float s, uint32_t c, float a)
{
    stroke_circle(cx, cy, s * 0.44f, s * 0.09f, c, a);
    aa_line(cx, cy, cx, cy - s * 0.26f, s * 0.09f, c, a);
    aa_line(cx, cy, cx + s * 0.19f, cy + s * 0.07f, s * 0.09f, c, a);
}
static void glyph_drive(float cx, float cy, float s, uint32_t c, float a)
{
    stroke_rr(cx - s * 0.46f, cy - s * 0.27f, s * 0.92f, s * 0.54f, s * 0.10f, s * 0.09f, c, a);
    fill_circle(cx + s * 0.26f, cy, s * 0.07f, c, a);
}
static void glyph_look(float cx, float cy, float s, uint32_t c, float a)
{
    stroke_rr(cx - s * 0.46f, cy - s * 0.36f, s * 0.92f, s * 0.62f, s * 0.07f, s * 0.09f, c, a);
    aa_line(cx - s * 0.16f, cy + s * 0.42f, cx + s * 0.16f, cy + s * 0.42f, s * 0.09f, c, a);
}
static void glyph_home(float cx, float cy, float s, uint32_t c, float a)
{
    /* Windows and her files: a house; they stay where they live */
    aa_line(cx - s * 0.44f, cy - s * 0.02f, cx, cy - s * 0.42f, s * 0.09f, c, a);
    aa_line(cx, cy - s * 0.42f, cx + s * 0.44f, cy - s * 0.02f, s * 0.09f, c, a);
    stroke_rr(cx - s * 0.30f, cy - s * 0.12f, s * 0.60f, s * 0.52f, s * 0.04f, s * 0.09f, c, a);
}
static void glyph_down(float cx, float cy, float s, uint32_t c, float a)
{
    /* the download: an arrow into a tray */
    aa_line(cx, cy - s * 0.42f, cx, cy + s * 0.12f, s * 0.09f, c, a);
    aa_line(cx - s * 0.20f, cy - s * 0.08f, cx, cy + s * 0.12f, s * 0.09f, c, a);
    aa_line(cx + s * 0.20f, cy - s * 0.08f, cx, cy + s * 0.12f, s * 0.09f, c, a);
    aa_line(cx - s * 0.40f, cy + s * 0.40f, cx + s * 0.40f, cy + s * 0.40f, s * 0.09f, c, a);
}
static void glyph_lock(float cx, float cy, float s, uint32_t c, float a)
{
    /* the drive lock: a padlock */
    stroke_rr(cx - s * 0.36f, cy - s * 0.04f, s * 0.72f, s * 0.46f, s * 0.07f, s * 0.09f, c, a);
    glyph_arc(cx, cy - s * 0.06f, s * 0.22f, 3.14159f, 6.28318f, s * 0.09f, c, a);
    aa_line(cx - s * 0.22f, cy - s * 0.06f, cx - s * 0.22f, cy - s * 0.04f, s * 0.09f, c, a);
    aa_line(cx + s * 0.22f, cy - s * 0.06f, cx + s * 0.22f, cy - s * 0.04f, s * 0.09f, c, a);
}
static void glyph_info(float cx, float cy, float s, uint32_t c, float a)
{
    stroke_circle(cx, cy, s * 0.44f, s * 0.08f, c, a);
    aa_line(cx, cy - s * 0.02f, cx, cy + s * 0.22f, s * 0.09f, c, a);
    fill_circle(cx, cy - s * 0.20f, s * 0.06f, c, a);
}

/* A glyph, and a sentence beside it. The shape the Ready page is made of.
 * The glyphs are ink-2: they help her find a line again, and should not
 * be what she looks at first. */
enum { G_HOME = 0, G_UNDO, G_CLOCK, G_DRIVE, G_LOOK, G_DOWN, G_INFO, G_LOCK };
static void glyph_at(int glyph, float cx, float cy, float gs, uint32_t c)
{
    switch (glyph) {
    case G_HOME:  glyph_home (cx, cy, gs, c, 1.f); break;
    case G_UNDO:  glyph_undo (cx, cy, gs, c, 1.f); break;
    case G_CLOCK: glyph_clock(cx, cy, gs, c, 1.f); break;
    case G_DRIVE: glyph_drive(cx, cy, gs, c, 1.f); break;
    case G_LOOK:  glyph_look (cx, cy, gs, c, 1.f); break;
    case G_DOWN:  glyph_down (cx, cy, gs, c, 1.f); break;
    case G_INFO:  glyph_info (cx, cy, gs, c, 1.f); break;
    case G_LOCK:  glyph_lock (cx, cy, gs, c, 1.f); break;
    default: break;
    }
}
static int glyph_row(int glyph, const wchar_t *text, int x, int y, int w)
{
    int d  = S(30);
    int tx = x + d + S(16);
    glyph_at(glyph, (float)x + (float)d * 0.5f, (float)y + (float)TYPE[type_of(g_f_body)].line * 0.5f,
             (float)S(28), C_INK2);
    return text_draw(text, g_f_body, C_INK, tx, y, w - (tx - x), DT_WORDBREAK);
}

/* A page's title, and the one sentence under it, if it has one. */
static int page_head(const wchar_t *title, const wchar_t *sub, int x, int y, int w)
{
    int y0 = y;
    y += text_draw(title, g_f_title, C_INK, x, y, w, DT_WORDBREAK);
    if (sub) y += S(8) + text_draw(sub, g_f_body, C_INK2, x, y + S(8), w > S(720) ? S(720) : w,
                                   DT_WORDBREAK);
    return y - y0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  1 · WELCOME, AND THE CHECK
 *
 *  What AurOS is, what happens to Windows, and the check -- running by
 *  itself, on the same screen, from the moment the window opens. She
 *  used to read a page, press Get started, and watch a second page
 *  check; now the check is done while she reads the two sentences.
 *  It used to show five little labels ticking (Power, Space, Drive,
 *  Start-up, Windows); they were words she would have to understand and
 *  could do nothing with, so it says one thing: looking, or ready.
 * ═══════════════════════════════════════════════════════════════════ */

/* What a check result means to her, for warnings: one line each. */
static const wchar_t *warn_line(const pf_result *r, wchar_t *buf, int cch)
{
    const wchar_t *n = fix_note(r->id);
    if (n) return n;
    a2w(r->title, buf, cch);
    return buf;
}

/* An icon, a bold line and a quieter one under it: the check's answer,
 * and each line of the stop page. Returns the height. */
static int status_row(int st, const wchar_t *head, const wchar_t *sub,
                      int x, int y, int w)
{
    int d = S(30), tx = x + d + S(16), tw = w - (tx - x);
    int hl = TYPE[type_of(g_f_bodyb)].line;
    status_icon((float)x + (float)d * 0.5f, (float)y + (float)hl * 0.5f, (float)S(13), st);
    int h = text_draw(head, g_f_bodyb, C_INK, tx, y, tw, DT_WORDBREAK);
    if (sub && sub[0]) h += S(2) + text_draw(sub, g_f_body, C_INK2, tx, y + h + S(2), tw,
                                             DT_WORDBREAK);
    return h;
}

/* SOMETHING SHE CAN'T PRESS looks like it: no border, a 4 px bar at its
 * left, an "i", and ink-2 words (DESIGN-DIRECTION.md §3). */
static int note_box(const wchar_t *head, const wchar_t *const *lines, int n,
                    int x, int y, int w)
{
    int y0 = y, tx = x + S(16) + S(30), tw = w - (tx - x);
    int lh = TYPE[type_of(g_f_body)].line;
    int h = head ? TYPE[type_of(g_f_bodyb)].line + S(4) : 0;
    for (int i = 0; i < n; i++) h += text_h(lines[i], g_f_body, tw, DT_WORDBREAK) + S(4);
    fill_rr((float)x, (float)y, (float)S(4), (float)h, (float)S(2), C_RULE, 1.f);
    glyph_info((float)(x + S(16) + S(12)), (float)y + (float)lh * 0.5f, (float)S(24), C_INK2, 1.f);
    if (head) y += text_draw(head, g_f_bodyb, C_INK, tx, y, tw, DT_SINGLELINE) + S(4);
    for (int i = 0; i < n; i++)
        y += text_draw(lines[i], g_f_body, C_INK2, tx, y, tw, DT_WORDBREAK) + S(4);
    return y - y0;
}

static void style_picture(int kind, float x, float y, float w, float h);

static int page_welcome(int x, int y, int w)
{
    int y0 = y;
    int narrow = w > S(720) ? S(720) : w;
    y += page_head(L"Let’s put AurOS on this computer",
                   L"AurOS is a free, simple new way to use this computer. AurOS is made to "
                   L"keep Windows and your files. If AurOS isn’t for you, you can go back "
                   L"to Windows.", x, y, w);
    y += S(g_short ? 24 : 40);

    int done = g_pf_valid && !g_checking;
    if (!done)
        y += status_row(ST_WORKING, L"Looking at this computer…",
                        L"This only looks. Nothing on this computer is changed.", x, y, narrow);
    else if (pf_is_go(&g_report)) {
        /* FROM HERE THE DOWNLOAD IS RUNNING (check_passed, download_begin),
         * so "this only looks" is no longer true, and her internet may have
         * a limit: say so on this page, where the check ends (TERMS 4) */
        wchar_t sub[420];
        wcscpy(sub, L"Next, you pick how it looks.");
        if (g_dl_state == 1)
            wcscat(sub, L" AurOS is downloading while you read: about 2\u00A0GB to download, "
                        L"and about 5\u00A0GB more once it is unpacked. If your internet has a data "
                        L"limit, close this window: closing deletes it.");
        else if (g_dl_state == 2)
            wcscat(sub, L" AurOS has downloaded: about 2\u00A0GB, and about 5\u00A0GB more once it is "
                        L"unpacked. If you close this window, it is deleted.");
        y += status_row(ST_DONE, L"This computer can take AurOS.", sub, x, y, narrow);
    }
    else
        y += status_row(ST_TODO, L"Something needs sorting out first.", NULL, x, y, narrow);

    /* Anything worth knowing that does not stop it: said here, one line
     * each, on the page she is looking at, before she agrees to anything
     * (docs/EASY.md, rule 9). */
    if (done && pf_is_go(&g_report)) {
        const wchar_t *lines[10];
        static wchar_t bufs[8][128];
        static wchar_t hwb[2][330];
        int n = 0;
        for (int i = 0; i < g_report.n && n < 8; i++) {
            const pf_result *r = &g_report.results[i];
            if (r->sev != PF_WARN) continue;
            const wchar_t *l = warn_line(r, bufs[n], 128);
            int dup = 0;
            for (int k = 0; k < n; k++) if (!wcscmp(lines[k], l)) dup = 1;
            if (!dup) lines[n++] = l;
        }
        /* Wi-Fi that will not work, with a cable socket that will: a note,
         * not a stop (HW) */
        n += hw_internet_notes(lines + n, 2, hwb);
        if (n) {
            y += S(g_short ? 20 : 32);
            y += note_box(L"Good to know", lines, n, x, y, narrow);
        }
    }

    /* WHAT SHE IS GETTING, a picture of it (B-16): the look like Windows, up
     * to 360 px wide, where there is room under the rest; on a short window
     * it is left out rather than scrolled to. The page is her first sight of
     * the product and a picture says what a sentence cannot. */
    {
        int room = (g_body.bottom - g_body.top) - (y - y0) - S(16) - S(28) - S(34);
        int ph = S(225);
        if (ph > room) ph = room;
        if (ph >= S(150)) {
            int pw = ph * 16 / 10;
            y += S(28);
            style_picture(PIC_WINDOWS, (float)x, (float)y, (float)pw, (float)ph);
            stroke_rr((float)x, (float)y, (float)pw, (float)ph, (float)S(4), 1.f, C_RULE, 1.f);
            y += ph + S(8);
            y += text_draw(L"AurOS, as it will look beside Windows. You pick the look next.",
                           g_f_small, C_INK2, x, y, narrow, DT_SINGLELINE);
        }
    }
    return y - y0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  1b · BLOCKED
 *
 *  A refusal is a product outcome, not an error dialog. It gets the same
 *  care as the happy path: what we found, what it means for this person,
 *  and the one thing they can do about it. There is no continue button,
 *  no "advanced", no override, and nav_allowed() would refuse one anyway.
 * ═══════════════════════════════════════════════════════════════════ */
/* THE ONE SETTING, DRAWN. A refusal whose remedy is a screen the
 * person has never seen needs more than a sentence, so the Secure Boot
 * card carries a sketch: the way there from Windows, and the firmware's
 * own screen with the one line to change marked. Every maker's screen
 * is a little different; the sketch uses the commonest words and the
 * card's text names the others. draw=0 measures only. */
/* A drawn chevron, never a typed one: the face has none (DESIGN-DIRECTION
 * §3, "arrows are drawn as icons"). Width about 0.6 s. */
static void chevron(float cx, float cy, float s, uint32_t col)
{
    aa_line(cx - s * 0.22f, cy - s * 0.42f, cx + s * 0.22f, cy, (float)S(2), col, 1.f);
    aa_line(cx + s * 0.22f, cy, cx - s * 0.22f, cy + s * 0.42f, (float)S(2), col, 1.f);
}

/* "a", a chevron, "b" on one line, vertically centred in [y, y+h) */
static void text_chev(const wchar_t *a, const wchar_t *b, HFONT f, uint32_t col, int x, int y, int h,
                      int maxw)
{
    RECT ra = { x, y, x + maxw, y + h };
    text_in(a, f, col, ra, DT_SINGLELINE | DT_VCENTER);
    int ax = x + text_w(a, f) + S(10);
    chevron((float)ax + (float)S(6), (float)y + (float)h * 0.5f, (float)S(18), col);
    RECT rb = { ax + S(22), y, x + maxw, y + h };
    text_in(b, f, col, rb, DT_SINGLELINE | DT_VCENTER);
}

static int sb_figure(int x, int y, int w, int draw)
{
    static const wchar_t *const way[] = {
        L"Settings", L"System", L"Recovery", L"Advanced startup: Restart now",
        L"Troubleshoot", L"Advanced options", L"UEFI Firmware Settings",
    };
    const int nway = (int)(sizeof way / sizeof way[0]);
    int y0 = y;
    int line = TYPE[type_of(g_f_small)].line;
    int chip_h = S(34), pad = S(12), gap = S(8);
    int sep_w = S(26);

    if (draw) text_draw(L"The way there, from Windows", g_f_smallb, C_INK, x, y, w, DT_SINGLELINE);
    y += line + S(6);
    int cx = x;
    for (int i = 0; i < nway; i++) {
        int cw = text_w(way[i], g_f_small) + pad * 2;
        if (cx > x && cx + cw > x + w) { cx = x; y += chip_h + gap; }
        if (draw) {
            int last = i == nway - 1;
            fill_rr((float)cx, (float)y, (float)cw, (float)chip_h, (float)S(8), C_SOFT, 1.f);
            if (last)
                stroke_rr((float)cx, (float)y, (float)cw, (float)chip_h, (float)S(8),
                          (float)S(2), C_WARN, 1.f);
            RECT b = { cx, y, cx + cw, y + chip_h };
            text_in(way[i], last ? g_f_smallb : g_f_small, C_INK, b,
                    DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            if (i < nway - 1) {
                RECT sb = { cx + cw, y, cx + cw + sep_w, y + chip_h };
                (void)sb;
                chevron((float)(cx + cw) + (float)sep_w * 0.5f, (float)y + (float)chip_h * 0.5f,
                        (float)S(18), C_INK2);
            }
        }
        cx += cw + sep_w;
    }
    y += chip_h + S(22);

    if (draw) text_draw(L"Then, on the computer’s own settings screen", g_f_smallb, C_INK,
                        x, y, w, DT_SINGLELINE);
    y += line + S(6);
    int row = S(36), sw = w > S(600) ? S(600) : w;
    int sh = row * 4 + S(10);
    if (draw) {
        fill_rr((float)x, (float)y, (float)sw, (float)sh, (float)S(8), C_SURFACE, 1.f);
        stroke_rr((float)x, (float)y, (float)sw, (float)sh, (float)S(8), 1.f, C_RULE, 1.f);
        fill_rr((float)x + 1.f, (float)y + 1.f, (float)sw - 2.f, (float)row, (float)S(7),
                C_SOFT, 1.f);
        RECT hb = { x + S(14), y, x + sw - S(14), y + row };
        text_chev(L"Security", L"Secure Boot", g_f_smallb, C_INK, hb.left, hb.top, row,
                  hb.right - hb.left);
        static const struct { const wchar_t *k, *v; int mark; } R[] = {
            { L"Secure Boot",                        L"[Enabled]  leave it on", 0 },
            { L"Allow Microsoft 3rd Party UEFI CA",  L"[Disabled]|[Enabled]", 1 },
            { L"Secure Boot Mode",                   L"[Standard]", 0 },
        };
        for (int i = 0; i < 3; i++) {
            int ry = y + row * (i + 1) + S(4);
            if (R[i].mark) {
                fill_rr((float)(x + S(6)), (float)ry, (float)(sw - S(12)), (float)(row - S(2)),
                        (float)S(5), C_WARN, 0.10f);
                stroke_rr((float)(x + S(6)), (float)ry, (float)(sw - S(12)), (float)(row - S(2)),
                          (float)S(5), (float)S(2), C_WARN, 1.f);
            }
            RECT kb = { x + S(16), ry, x + sw * 58 / 100, ry + row - S(2) };
            RECT vb = { x + sw * 60 / 100, ry, x + sw - S(14), ry + row - S(2) };
            text_in(R[i].k, R[i].mark ? g_f_smallb : g_f_small, C_INK, kb,
                    DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            if (wcschr(R[i].v, L'|')) {            /* "[from] chevron [to]" */
                wchar_t from[40], to[40];
                const wchar_t *bar = wcschr(R[i].v, L'|');
                wcsncpy(from, R[i].v, (size_t)(bar - R[i].v)); from[bar - R[i].v] = 0;
                wcscpy(to, bar + 1);
                text_chev(from, to, g_f_smallb, C_WARN, vb.left, vb.top, vb.bottom - vb.top,
                          vb.right - vb.left);
            } else {
                text_in(R[i].v, R[i].mark ? g_f_smallb : g_f_small, C_INK2,
                        vb, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            }
        }
    }
    y += sh + S(12);
    const wchar_t *cap =
        L"A sketch: every maker’s screen looks a little different. Then choose "
        L"Save and Exit (often F10), and run this installer again. Secure Boot "
        L"stays on.";
    y += draw ? text_draw(cap, g_f_small, C_INK2, x, y, w, DT_WORDBREAK)
              : text_h(cap, g_f_small, w, DT_WORDBREAK);
    return y - y0;
}

/* "One setting needs changing first" (DESIGN-DIRECTION.md B-5). What
 * she reads is two sentences; "Show a helper how" opens, on the same
 * page, the sketch of the way there and the technical paragraph, with
 * "Copy for a helper" so it can go in a message. */
static int g_sb_helper;                /* the helper's part is open        */
static int g_copied;                   /* "Copy for a helper" was pressed  */

static const pf_result *sb_result(void)
{
    for (int i = 0; i < g_report.n; i++)
        if (g_report.results[i].sev == PF_BLOCK && !strcmp(g_report.results[i].id, SBDB_BLOCK_ID))
            return &g_report.results[i];
    return NULL;
}

/* The page is the Secure Boot page when that is the only stop that
 * nothing on this computer can fix. */
static int sb_page(void)
{
    if (!sb_result()) return 0;
    for (int i = 0; i < g_report.n; i++) {
        const pf_result *r = &g_report.results[i];
        if (r->sev == PF_BLOCK && strcmp(r->id, SBDB_BLOCK_ID) && kind_of(r) == FIX_CANNOT) return 0;
    }
    return 1;
}

static void sb_helper_text(wchar_t *out, int n)
{
    const pf_result *r = sb_result();
    wchar_t d[600] = L"", m[600] = L"";
    if (r) { a2w(r->detail, d, 600); a2w(r->remedy, m, 600); }
    _snwprintf(out, (size_t)n, L"%ls %ls", d, m);
    out[n - 1] = 0;
}

static int sb_helper(int x, int y, int w)
{
    int y0 = y;
    y += text_draw(L"For a helper", g_f_h3, C_INK, x, y, w, DT_SINGLELINE) + S(12);
    y += sb_figure(x, y, w, 1) + S(24);
    static wchar_t t[1300];
    sb_helper_text(t, 1300);
    y += text_draw(t, g_f_small, C_INK2, x, y, w, DT_WORDBREAK) + S(16);
    int bw = text_w(L"Copy for a helper", g_f_btn) + S(48);
    draw_button(ID_COPY, L"Copy for a helper", x, y, bw, S(48), 0, 0);
    if (g_copied) {
        RECT b = { x + bw + S(16), y, x + w, y + S(48) };
        text_in(L"Copied. Paste it into a message to your helper.", g_f_small, C_INK2, b,
                DT_SINGLELINE | DT_VCENTER);
    }
    y += S(48);
    return y - y0;
}

static void copy_for_helper(void)
{
    static wchar_t t[1300];
    sb_helper_text(t, 1300);
    size_t n = (wcslen(t) + 1) * sizeof(wchar_t);
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n);
    void *p = g ? GlobalLock(g) : NULL;
    if (p) {
        memcpy(p, t, n);
        GlobalUnlock(g);
        if (SetClipboardData(CF_UNICODETEXT, g)) g = NULL;
    }
    if (g) GlobalFree(g);
    CloseClipboard();
    g_copied = 1;
}

/* "Save a report for a helper": what is inside this computer (HW's
 * AurOS-report.txt and .json) on her Desktop, written on a worker (the read
 * takes a couple of seconds). Read-only toward the computer. */
static volatile LONG g_rep_state;      /* 0 idle, 1 saving, 2 saved, 3 could not */
static int           g_nt_reports;

static DWORD WINAPI report_worker(LPVOID p)
{
    (void)p;
    WCHAR d[MAX_PATH];
    char dir[MAX_PATH * 3] = "";
    if (SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, d) == S_OK)
        WideCharToMultiByte(CP_UTF8, 0, d, -1, dir, sizeof dir, NULL, NULL);
    int rc = dir[0] ? hwinv_report_main(dir, 0, NULL) : 1;
    InterlockedExchange(&g_rep_state, rc == 0 ? 2 : 3);
    return 0;
}

static void start_report(void)
{
    if (g_nav_mode || g_shot_mode) { g_nt_reports++; return; }
    if (g_rep_state == 1) return;
    InterlockedExchange(&g_rep_state, 1);
    HANDLE h = CreateThread(NULL, 0, report_worker, NULL, 0, NULL);
    if (h) CloseHandle(h);
    else InterlockedExchange(&g_rep_state, 3);
}

/* The helper's page in her browser, opened as her and not as the
 * administrator this program runs as: through Explorer. */
static const wchar_t *helper_url(void)
{
    for (int i = 0; i < g_report.n; i++)
        if (g_report.results[i].sev == PF_BLOCK) {
            const wchar_t *u = fix_helper_url(g_report.results[i].id);
            if (u) return u;
        }
    return NULL;
}

static void open_helper(void)
{
    const wchar_t *u = helper_url();
    if (!u || g_nav_mode || g_shot_mode || g_selftest) return;
    ShellExecuteW(g_hwnd, L"open", L"explorer.exe", u, NULL, SW_SHOWNORMAL);
}

/* THE SPACE FIX, SAID WITH ITS SIZE before she presses: what it deletes
 * and how much, or, when that is next to nothing, what she can do. */
static void space_fix_line(wchar_t *out, int n)
{
    if (g_temp_bytes >= TEMP_WORTH) {
        wchar_t sz[32];
        if (g_temp_bytes >= 1000000000ULL)
            _snwprintf(sz, 31, L"%.1f GB", (double)g_temp_bytes / 1e9);
        else
            _snwprintf(sz, 31, L"%d MB", (int)((g_temp_bytes + 5000000ULL) / 10000000ULL) * 10);
        sz[31] = 0;
        _snwprintf(out, (size_t)n, L"We delete %ls of Windows\u2019 old temporary files. "
                                   L"Nothing of yours.", sz);
    } else {
        _snwprintf(out, (size_t)n, L"Move big files (videos, games) off drive C:, or empty "
                                   L"the Recycle Bin. We carry on by ourselves.");
    }
    out[n - 1] = 0;
}

/* What one stop says, and what happens about it, right now. */
static int stop_line(const pf_result *r, int cannot, wchar_t *prob, wchar_t *fix)
{
    const fix_info *f = fix_lookup(r->id);
    if (f) {
        wcsncpy(prob, f->problem, 159); prob[159] = 0;
        wcsncpy(fix, f->fix, 599);      fix[599] = 0;
        if (!strcmp(r->id, "insufficient-space")) space_fix_line(fix, 600);
    } else {
        /* A check this table has never heard of still gets one short
         * line, never the check's own paragraph. */
        a2w(r->title, prob, 160);
        wcscpy(fix, L"AurOS stops here. Nothing was changed.");
    }
    /* HW's sentence for this PC, as hers: which card, and what fixes it */
    if (!strcmp(r->id, "no-internet") && hw_block_says()[0])
        MultiByteToWideChar(CP_UTF8, 0, hw_block_says(), -1, fix, 599);
    int st = kind_of(r) == FIX_WAIT || kind_of(r) == FIX_CANNOT || !f ? ST_TODO : ST_PENDING;
    int j = -1;
    for (int k = 0; k < g_fx_n; k++) if (!strcmp(g_fx[k].id, r->id)) j = k;
    fix_kind k = kind_of(r);
    if (cannot && k != FIX_CANNOT && f && f->kind != FIX_WAIT)
        wcscpy(fix, L"We’ll fix this one after the ones above.");
    if (f && f->kind == FIX_RESTART && k == FIX_CANNOT)
        wcscpy(fix, L"Restarting didn’t clear it, so AurOS can’t go on yet.");
    if (j >= 0 && g_fx[j].state == FS_RUNNING) {
        wcscpy(fix, f && f->kind == FIX_RESTART
                    ? L"Restarting Windows now. AurOS Setup opens again by itself."
                    : L"Fixing it now. This can take a few minutes.");
        st = ST_WORKING;
    } else if (j >= 0 && g_fx[j].state == FS_DONE && g_fx[j].after && f && f->after) {
        wcsncpy(fix, f->after, 599); fix[599] = 0;
        st = ST_TODO;
    } else if (j >= 0 && g_fx[j].state == FS_DONE) {
        wcscpy(fix, L"Done. Checking again.");
        st = ST_DONE;
    } else if (j >= 0 && g_fx[j].state == FS_FAILED) {
        wcscpy(fix, plain_failure(g_fx[j].why));
        st = ST_TODO;
    }
    return st;
}

static int page_blocked(int x, int y, int w)
{
    int y0 = y;
    int narrow = w > S(760) ? S(760) : w;
    int cannot = 0;
    for (int i = 0; i < g_report.n; i++)
        if (g_report.results[i].sev == PF_BLOCK && kind_of(&g_report.results[i]) == FIX_CANNOT)
            cannot++;

    if (sb_page()) {
        y += page_head(L"One setting needs changing first",
                       L"This computer is set to start only Windows. Someone who knows "
                       L"computers can change it in about five minutes. Nothing on this "
                       L"computer has been changed.", x, y, w);
        if (g_sb_helper) { y += S(32); y += sb_helper(x, y, narrow); }
        return y - y0;
    }

    y += page_head(cannot ? L"AurOS can’t go on this computer yet"
                          : L"A few things to sort out first",
                   L"Nothing on this computer has been changed.", x, y, w);

    /* Two lists, so it is plain who does what: what she does, and what
     * the button does for her. When something cannot be fixed, only that,
     * and what she can do instead. */
    static const wchar_t *const heads[3] = { L"You:", L"We do these for you:", NULL };
    for (int list = 0; list < 3; list++) {
        int rows = 0;
        for (int i = 0; i < g_report.n; i++) {
            const pf_result *r = &g_report.results[i];
            if (r->sev != PF_BLOCK) continue;
            fix_kind k = kind_of(r);
            int mine = (k == FIX_WAIT || k == FIX_UNKNOWN);
            if (cannot ? (list != 2 || k != FIX_CANNOT)
                       : (list == 2 || (list == 0) != mine)) continue;
            if (!rows++) {
                y += S(g_short ? 20 : 28);
                if (heads[list])
                    y += text_draw(heads[list], g_f_bodyb, C_INK, x, y, narrow,
                                   DT_SINGLELINE) + S(8);
            } else {
                y += S(12);
            }
            wchar_t prob[160], fix[600];
            int st = stop_line(r, cannot, prob, fix);
            y += status_row(st, prob, fix, x, y, narrow);
        }
    }

    if (cannot) {
        const wchar_t *said[8];
        int n_said = 0;
        for (int i = 0; i < g_report.n && n_said < 8; i++) {
            const pf_result *r = &g_report.results[i];
            if (r->sev != PF_BLOCK || kind_of(r) != FIX_CANNOT) continue;
            const wchar_t *t = fix_instead(r->id);
            if (!t) t = L"Keep using Windows as it is. Nothing on this computer was changed.";
            int dup = 0;
            for (int k = 0; k < n_said; k++) if (said[k] == t || !wcscmp(said[k], t)) dup = 1;
            if (!dup) said[n_said++] = t;
        }
        y += S(g_short ? 20 : 32);
        y += note_box(L"What you can do", said, n_said, x, y, narrow);
    }
    return y - y0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  2 · PICK THE LOOK YOU WANT
 *
 *  Four pictures, each the look's own home screen drawn small: the
 *  layout aurshell gives that look (docs/shots/shells/<shell>.png) in
 *  Sandstone's colours, the theme each card installs, so what she picks
 *  is what she gets. They are shapes, not bitmaps, so a 150% laptop gets
 *  them as crisp as a 100% one. One is chosen before she arrives (the
 *  one like what she knows), so "Next" is always a right answer.
 * ═══════════════════════════════════════════════════════════════════ */

/* A word, drawn as a stub of text: the pictures have no letters. */
static void pv_text(float x, float y, float w, float h, uint32_t c, float a)
{
    fill_rr(x, y, w, h, h * 0.5f, c, a);
}

/* The thin bar along the top (every look but the one like Windows):
 * the name at the left, the date and time at the right. */
static void pv_top(float x, float y, float w, float h)
{
    float bh = h * 0.06f;
    fill_rr(x, y, w, bh, 0.f, LT_SOFT, 1.f);
    aa_line(x, y + bh, x + w, y + bh, 1.f, LT_RULE, 0.7f);
    fill_diamond(x + w * 0.022f, y + bh * 0.5f, bh * 0.22f, LT_GO, 1.f);
    pv_text(x + w * 0.04f, y + bh * 0.40f, w * 0.05f, bh * 0.22f, LT_INK, 0.6f);
    pv_text(x + w * 0.88f, y + bh * 0.38f, w * 0.09f, bh * 0.26f, LT_INK, 0.7f);
}

/* The strip along the bottom of every look, in its fixed dark colours:
 * Help, Wi-Fi and Settings at the left; Turn off, Bigger and Smaller
 * text at the right (src/aurshell/foot.c). */
static void pv_help(float x, float y, float w, float h)
{
    float bh = h * 0.075f, by = y + h - bh;
    fill_rr(x, by, w, bh, 0.f, FOOT_BG, 1.f);
    float th = bh * 0.22f, ty = by + (bh - th) * 0.5f;
    static const float L[][2] = { { 0.014f, 0.035f }, { 0.094f, 0.035f }, { 0.173f, 0.052f },
                                  { 0.770f, 0.050f }, { 0.849f, 0.045f }, { 0.929f, 0.050f } };
    for (int i = 0; i < 6; i++) pv_text(x + w * L[i][0], ty, w * L[i][1], th, FOOT_INK, 0.9f);
}

/* a window: a card, a title strip, a few lines of text */
static void pv_window(float x, float y, float w, float h, int front)
{
    float r = (float)S(2);
    fill_rr(x, y, w, h, r, LT_CARD, 1.f);
    stroke_rr(x, y, w, h, r, 1.f, LT_RULE, front ? 1.f : 0.6f);
    float th = h * 0.15f;
    fill_rr(x + 1.f, y + 1.f, w - 2.f, th, r, LT_SOFT, 1.f);
    pv_text(x + w * 0.05f, y + th * 0.38f, w * 0.22f, th * 0.26f, LT_INK, 0.55f);
    fill_rr(x + w - th * 0.85f, y + th * 0.30f, th * 0.40f, th * 0.40f, 1.f, LT_RULE, 0.8f);
    if (!front) return;
    for (int i = 0; i < 3; i++)
        pv_text(x + w * 0.07f, y + th + h * (0.14f + 0.14f * (float)i),
                w * (i == 2 ? 0.42f : 0.74f), h * 0.05f, LT_TEXT, 1.f);
}

/* a program's symbol, as the looks draw them: a plain shape in ink */
static void pv_icon(float cx, float cy, float s, int kind, uint32_t c)
{
    switch (kind % 3) {
    case 0:  fill_circle(cx, cy, s * 0.5f, c, 1.f); break;                    /* the web */
    case 1:  fill_rr(cx - s * 0.5f, cy - s * 0.38f, s, s * 0.76f, s * 0.08f, c, 1.f); break;
    default: fill_rr(cx - s * 0.36f, cy - s * 0.5f, s * 0.72f, s, s * 0.06f, c, 1.f); break;
    }
}

static void style_picture(int kind, float x, float y, float w, float h)
{
    GdiFlush();
    RECT keep = g_clip;
    if ((int)x > g_clip.left) g_clip.left = (int)x;
    if ((int)y > g_clip.top) g_clip.top = (int)y;
    if ((int)(x + w) < g_clip.right) g_clip.right = (int)(x + w);
    if ((int)(y + h) < g_clip.bottom) g_clip.bottom = (int)(y + h);
    fill_rr(x, y, w, h, 0.f, LT_PAPER, 1.f);
    float ink = 0.85f;
    switch (kind) {
    case PIC_WINDOWS: {
        /* Like Windows: windows on the desktop, and the bar along the
         * bottom with All my programs at its left and the clock at its right */
        pv_window(x + w * 0.05f, y + h * 0.06f, w * 0.44f, h * 0.44f, 0);
        pv_window(x + w * 0.24f, y + h * 0.15f, w * 0.58f, h * 0.54f, 1);
        float bh = h * 0.10f, by = y + h * 0.925f - bh;
        fill_rr(x, by, w, bh, 0.f, LT_SOFT, 1.f);
        aa_line(x, by, x + w, by, 1.f, LT_RULE, 0.8f);
        float bb = by + bh * 0.18f, bt = bh * 0.64f;
        stroke_rr(x + w * 0.012f, bb, w * 0.15f, bt, 1.f, 1.f, LT_INK, 0.9f);
        fill_diamond(x + w * 0.030f, bb + bt * 0.5f, bt * 0.20f, LT_GO, 1.f);
        pv_text(x + w * 0.048f, bb + bt * 0.40f, w * 0.10f, bt * 0.20f, LT_INK, 0.7f);
        fill_rr(x + w * 0.18f, bb, w * 0.15f, bt, 1.f, LT_CARD, 1.f);
        stroke_rr(x + w * 0.18f, bb, w * 0.15f, bt, 1.f, 1.f, LT_RULE, 0.6f);
        pv_text(x + w * 0.195f, bb + bt * 0.40f, w * 0.10f, bt * 0.20f, LT_INK, 0.55f);
        pv_text(x + w * 0.90f, by + bh * 0.30f, w * 0.07f, bh * 0.18f, LT_INK, 0.8f);
        pv_text(x + w * 0.91f, by + bh * 0.58f, w * 0.06f, bh * 0.12f, LT_INK, 0.5f);
        break;
    }
    case PIC_MAC: {
        /* Like a Mac: a thin bar at the top, and the row of programs
         * along the bottom */
        pv_top(x, y, w, h);
        pv_window(x + w * 0.16f, y + h * 0.13f, w * 0.68f, h * 0.53f, 1);
        float dw = w * 0.62f, dh = h * 0.13f;
        float dx = x + (w - dw) * 0.5f, dy = y + h * 0.765f;
        fill_rr(dx, dy, dw, dh, 1.f, LT_CARD, 1.f);
        stroke_rr(dx, dy, dw, dh, 1.f, 1.f, LT_RULE, 0.8f);
        for (int i = 0; i < 6; i++)
            pv_icon(dx + dw * (0.10f + 0.16f * (float)i), dy + dh * 0.5f, dh * 0.50f, i,
                    i == 5 ? LT_GO : LT_INK);
        break;
    }
    case PIC_SIMPLE: {
        /* Big and simple: one page in the middle that asks "What would
         * you like to do?", and a short list of big rows, all the same */
        pv_top(x, y, w, h);
        float px = x + w * 0.19f, py = y + h * 0.10f, pw = w * 0.62f, ph = h * 0.77f;
        fill_rr(px, py, pw, ph, 1.f, LT_CARD, 1.f);
        stroke_rr(px, py, pw, ph, 1.f, 1.f, LT_RULE, 0.8f);
        pv_text(px + pw * 0.05f, py + ph * 0.07f, pw * 0.62f, ph * 0.07f, LT_INK, 0.9f);
        float ry = py + ph * 0.22f, rh = ph * 0.125f;
        for (int i = 0; i < 6; i++) {
            float cy = ry + rh * ((float)i + 0.5f);
            pv_icon(px + pw * 0.10f, cy, rh * 0.50f, i, i == 5 ? LT_GO : LT_INK);
            pv_text(px + pw * 0.18f, cy - rh * 0.09f, pw * 0.22f, rh * 0.18f, LT_INK, ink);
            pv_text(px + pw * 0.50f, cy - rh * 0.06f, pw * 0.34f, rh * 0.12f, LT_TEXT, 1.f);
            if (i < 5) aa_line(px + pw * 0.05f, ry + rh * (float)(i + 1), px + pw * 0.95f,
                               ry + rh * (float)(i + 1), 1.f, LT_RULE, 0.5f);
        }
        break;
    }
    case PIC_ONE: {
        /* One thing at a time: the programs as big tiles, a page of them
         * at a time, and Home at the bottom to come back to */
        pv_top(x, y, w, h);
        static const float T[][4] = {   /* x, y, w, h as parts of the picture */
            { 0.022f, 0.09f, 0.468f, 0.34f }, { 0.508f, 0.09f, 0.225f, 0.34f },
            { 0.751f, 0.09f, 0.225f, 0.34f },
            { 0.022f, 0.45f, 0.225f, 0.26f }, { 0.265f, 0.45f, 0.225f, 0.26f },
            { 0.508f, 0.45f, 0.225f, 0.26f }, { 0.751f, 0.45f, 0.225f, 0.26f },
        };
        for (int i = 0; i < 7; i++) {
            float tx = x + w * T[i][0], ty = y + h * T[i][1], tw = w * T[i][2], th = h * T[i][3];
            fill_rr(tx, ty, tw, th, 1.f, LT_CARD, 1.f);
            stroke_rr(tx, ty, tw, th, 1.f, 1.f, LT_RULE, 0.8f);
            if (i == 0) {
                fill_circle(tx + tw * 0.18f, ty + th * 0.48f, th * 0.20f, LT_INK, ink);
                pv_text(tx + tw * 0.38f, ty + th * 0.40f, tw * 0.32f, th * 0.09f, LT_INK, 0.85f);
            } else {
                pv_icon(tx + tw * 0.22f, ty + th * 0.26f, th * 0.22f, i, i == 6 ? LT_GO : LT_INK);
                pv_text(tx + tw * 0.10f, ty + th * 0.70f, tw * 0.55f, th * 0.09f, LT_INK, ink);
            }
        }
        float hb = y + h * 0.74f;
        stroke_rr(x + w * 0.022f, hb, w * 0.24f, h * 0.10f, 1.f, 1.f, LT_INK, 0.9f);
        pv_text(x + w * 0.045f, hb + h * 0.04f, w * 0.08f, h * 0.025f, LT_INK, 0.8f);
        break;
    }
    default: break;
    }
    pv_help(x, y, w, h);
    g_clip = keep;
}

/* THE LOOK CARDS (DESIGN-DIRECTION.md B-7): a 2x2 grid, all four always
 * on the screen, never a scroll bar. Each card is the picture and the
 * name; the chosen one has a green 3 px border, the selected fill, a
 * check mark and the word Chosen; one line under the grid says what the
 * chosen look is. The window decides how big the pictures can be:
 *   PICK_TALL   name under the picture (a 1366x768 laptop)
 *   PICK_WIDE   name beside a 150 px picture (a 1024x600 netbook)
 *   PICK_NAMES  names only, and one picture of the chosen look beside
 *               them (150% on a 1366x768 laptop, or bigger text) */
enum { PICK_TALL, PICK_WIDE, PICK_NAMES };
#define PICK_GAP 16

static void card_frame(int i, int x, int y, int w, int h)
{
    int sel = (g_sel_style == i);
    int idx = w_add(ID_STYLE + i, W_CARD, x, y, w, h, 1);
    if (idx >= 0) g_w[idx].name = STYLES[i].name;
    int hot = w_hot(idx);
    float r = (float)S(12);
    fill_rr((float)x, (float)y, (float)w, (float)h, r, sel ? C_SELECTED : C_SURFACE, 1.f);
    stroke_rr((float)x, (float)y, (float)w, (float)h, r,
              (float)S(sel ? 3 : 2), sel ? C_ACCENT : (hot ? C_INK : C_RULE), 1.f);
    if (w_focused(idx)) { RECT b = { x, y, x + w, y + h }; focus_ring(b, S(4), S(15)); }
}

/* the name, and under it "Chosen" with its check mark when it is */
static void card_name(int i, int x, int y, int w, int center_h)
{
    int nh = text_h(STYLES[i].name, g_f_h3, w, DT_WORDBREAK);
    int ch = g_sel_style == i ? TYPE[type_of(g_f_smallb)].line + S(2) : 0;
    if (center_h) y += (center_h - nh - ch) / 2;
    y += text_draw(STYLES[i].name, g_f_h3, C_INK, x, y, w, DT_WORDBREAK);
    if (g_sel_style == i) {
        int lh = TYPE[type_of(g_f_smallb)].line;
        float r = (float)S(10);
        fill_circle((float)x + r, (float)(y + S(2)) + (float)lh * 0.5f, r, C_ACCENT, 1.f);
        glyph_check((float)x + r, (float)(y + S(2)) + (float)lh * 0.5f, r * 1.15f, C_ON_ACCENT, 1.f);
        text_draw(L"Chosen", g_f_smallb, C_ACCENT, x + S(28), y + S(2), w - S(28), DT_SINGLELINE);
    }
}

/* "Chosen", with its check mark, on the picture's top right corner */
static void chosen_pill(int px, int py, int pw)
{
    int tw = text_w(L"Chosen", g_f_smallb), h = S(30), w = tw + S(44);
    int x = px + pw - w - S(8), y = py + S(8);
    /* outlined, not filled: the only solid green is the button to press */
    fill_rr((float)x, (float)y, (float)w, (float)h, (float)h * 0.5f, C_SURFACE, 1.f);
    stroke_rr((float)x, (float)y, (float)w, (float)h, (float)h * 0.5f, (float)S(2), C_ACCENT, 1.f);
    glyph_check((float)(x + S(16)), (float)y + (float)h * 0.5f, (float)S(16), C_ACCENT, 1.f);
    RECT b = { x + S(30), y, x + w, y + h };
    text_in(L"Chosen", g_f_smallb, C_ACCENT, b, DT_SINGLELINE | DT_VCENTER);
}

static int pick_mode(int w, int avail, int *pw)
{
    int col = (w - S(PICK_GAP)) / 2;
    int name = TYPE[type_of(g_f_h3)].line;
    int line = S(16) + TYPE[type_of(g_f_body)].line;
    /* tall: up to 16:10 pictures of 260 x 162, as tall as fits */
    int ph = (avail - line - S(PICK_GAP)) / 2 - S(12) * 2 - S(8) - name;
    if (ph > S(162)) ph = S(162);
    if (ph >= S(118) && ph * 16 / 10 <= col - S(24)) { *pw = ph * 16 / 10; return PICK_TALL; }
    /* wide: a 150 px picture, the name beside it */
    if ((S(94) + S(24)) * 2 + S(PICK_GAP) + line <= avail) { *pw = S(150); return PICK_WIDE; }
    *pw = 0;
    return PICK_NAMES;
}

static int page_style(int x, int y, int w)
{
    int y0 = y;
    y += page_head(L"Pick the look you want", NULL, x, y, w);
    y += S(g_short ? 18 : 24);
    int avail = g_body.bottom - g_body.top - (y - y0) - S(16);   /* render()'s own margin */
    int pw, mode = pick_mode(w, avail, &pw);
    int gap = S(PICK_GAP), pad = S(12);
    int gw = w, gx = x;
    int col, cardh, side_h = 0;
    if (mode == PICK_NAMES) {
        /* the names in a 2x2 at the left, the chosen look's picture beside */
        int ph = avail - S(16) - TYPE[type_of(g_f_body)].line * 2;
        int pw2 = ph * 16 / 10;
        if (pw2 > w / 2) { pw2 = w / 2; ph = pw2 * 10 / 16; }
        gw = w - pw2 - S(24);
        style_picture(STYLES[g_sel_style].picture, (float)(x + gw + S(24)), (float)y,
                      (float)pw2, (float)ph);
        stroke_rr((float)(x + gw + S(24)), (float)y, (float)pw2, (float)ph, (float)S(4), 1.f,
                  C_RULE, 1.f);
        col = (gw - gap) / 2;
        cardh = (ph - gap) / 2;
        if (cardh > S(96)) cardh = S(96);
        side_h = ph;
    } else if (mode == PICK_WIDE) {
        col = (gw - gap) / 2;
        cardh = pad * 2 + pw * 10 / 16;
    } else {
        col = (gw - gap) / 2;
        cardh = pad * 2 + pw * 10 / 16 + S(8) + TYPE[type_of(g_f_h3)].line;
    }
    for (int i = 0; i < N_STYLES; i++) {
        int cx = gx + (i % 2) * (col + gap), cy = y + (i / 2) * (cardh + gap);
        card_frame(i, cx, cy, col, cardh);
        if (mode == PICK_TALL) {
            int px = cx + (col - pw) / 2, ph = pw * 10 / 16;
            style_picture(STYLES[i].picture, (float)px, (float)(cy + pad), (float)pw, (float)ph);
            stroke_rr((float)px, (float)(cy + pad), (float)pw, (float)ph, (float)S(4), 1.f,
                      C_RULE, 1.f);
            if (g_sel_style == i) chosen_pill(px, cy + pad, pw);
            text_draw(STYLES[i].name, g_f_h3, C_INK, px, cy + pad + ph + S(8), pw, DT_SINGLELINE);
        } else if (mode == PICK_WIDE) {
            int ph = pw * 10 / 16;
            style_picture(STYLES[i].picture, (float)(cx + pad), (float)(cy + pad), (float)pw,
                          (float)ph);
            stroke_rr((float)(cx + pad), (float)(cy + pad), (float)pw, (float)ph, (float)S(4), 1.f,
                      C_RULE, 1.f);
            int tx = cx + pad + pw + S(16);
            card_name(i, tx, cy + pad, col - (tx - cx) - pad, ph);
        } else {
            card_name(i, cx + S(16), cy, col - S(32), cardh);
        }
    }
    y += cardh * 2 + gap > side_h ? cardh * 2 + gap : side_h;
    /* what the chosen one is, in a sentence */
    y += S(16);
    wchar_t line[160];
    _snwprintf(line, 159, L"Picked for you: %ls. Press another look to change it.",
               STYLES[g_sel_style].name);
    line[159] = 0;
    y += text_draw(line, g_f_body, C_INK, x, y, w, DT_WORDBREAK);
    return y - y0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  3 · READY
 *
 *  Under eighty words: what stays, where AurOS goes, the one restart,
 *  the way back, and how long it takes on THIS computer. One button.
 *  No word to type, no box to tick, no drive model number.
 * ═══════════════════════════════════════════════════════════════════ */
static const pf_disk *sys_disk(void)
{
    /* system_disk is a PhysicalDrive number, not a position in disks[]:
     * the two differ as soon as a number is skipped. */
    for (int i = 0; i < g_report.n_disks; i++)
        if (g_report.disks[i].index == g_report.system_disk) return &g_report.disks[i];
    return NULL;
}

/* ── How long, honestly ────────────────────────────────────────────
 *
 * The download is most of it (and size is known: the piece list baked
 * into this installer says it to the byte). After that it depends on
 * the drive: an SSD makes room and writes AurOS in a few minutes, a
 * hard disk takes several times as long, and preflight asks the drive
 * which it is (seek_penalty). These are for the install as the fast
 * path does it (docs/handoff/SESSION-2026-10-23.md, D1): the slow work
 * in Windows while the download runs, about a minute after the restart. */
/* THE TIME, FOR THE PATH THAT SHIPS (the install that makes room after the
 * restart: the fast path, which does it in Windows while the download runs,
 * is not selectable yet). NOT MEASURED: these are what the audit worked out
 * from the code (docs/research/journey-audit-2026-10-23.md), widened. The
 * journey clock on a real Windows VM (WP-40, stream F) replaces them. A
 * range that is too long costs her an impatient wait; one that is too
 * short costs her the person who switches it off at minute 30. */
#define ETA_LINK_BPS        3000000.0  /* 24 Mbit/s, until one is measured */
#define ETA_SSD_WORK_MIN    20         /* the checks, the restart, making room, copying, first start */
#define ETA_HDD_WORK_MIN    60
#define ETA_SSD_AFTER_MIN   20         /* what is left once the download is done */
#define ETA_HDD_AFTER_MIN   75
#define DL_BYTES_FALLBACK   1800000000ULL

static uint64_t download_bytes(void)
{
    static int known = -1;
    static uint64_t bytes;
    if (known < 0) {
        static ab_pieces p;
        char why[200];
        known = 0;
        if (AUROS_PIECES_TEXT[0] &&
            ab_pieces_parse(AUROS_PIECES_TEXT, &p, why, sizeof why) == 0 && p.total) {
            bytes = p.total;
            known = 1;
        }
    }
    return known ? bytes : DL_BYTES_FALLBACK;
}

/* -1 unknown, 0 SSD (no seek penalty), 1 a spinning disk */
static int disk_is_slow(void)
{
    const pf_disk *d = sys_disk();
    return d ? d->seek_penalty : -1;
}

static int after_restart_min(void)
{
    int s = disk_is_slow();
    return s == 1 ? ETA_HDD_AFTER_MIN : s == 0 ? ETA_SSD_AFTER_MIN : ETA_HDD_AFTER_MIN;
}

/* HER INTERNET, MEASURED. The download starts the moment the check
 * passes (download_begin), so by the Ready page there is usually a rate
 * to go on instead of an assumed one. Percent per second, 0 = not yet. */
static ULONGLONG g_dlm_t0;
static int       g_dlm_p0 = -1;
static double    g_dlm_rate;

static void dl_measure(ULONGLONG now)
{
    if (g_dl_state != 1 || !g_ab_lock_ready) return;
    EnterCriticalSection(&g_ab_lock);
    int pct = g_ab_pct;
    LeaveCriticalSection(&g_ab_lock);
    if (g_dlm_p0 < 0 || pct < g_dlm_p0) { g_dlm_t0 = now; g_dlm_p0 = pct; return; }
    if (now - g_dlm_t0 >= 8000 && pct - g_dlm_p0 >= 1)
        g_dlm_rate = (double)(pct - g_dlm_p0) / ((double)(now - g_dlm_t0) / 1000.0);
}

static void eta_range(int *lo, int *hi)
{
    /* The download: assumed (a wide range), measured (a narrow one), or
     * already done. */
    double dl = (double)download_bytes() / ETA_LINK_BPS / 60.0;
    double dl_lo = 0.5, dl_hi = 1.6;
    if (g_dl_state == 2) {
        dl = 0.0;
    } else if (g_dl_state == 1 && g_dlm_rate > 0.0 && g_ab_lock_ready) {
        EnterCriticalSection(&g_ab_lock);
        int pct = g_ab_pct;
        LeaveCriticalSection(&g_ab_lock);
        dl = (100.0 - (double)pct) / g_dlm_rate / 60.0;
        dl_lo = 0.9; dl_hi = 1.3;
    }
    int s = disk_is_slow();
    double w = (s == 0 ? ETA_SSD_WORK_MIN : ETA_HDD_WORK_MIN);
    double lo_m = dl * dl_lo + w;
    double hi_m = dl * dl_hi + w * 2.0;          /* twice what the code says: it is a guess */
    if (g_dl_state == 2) { lo_m = (s == 0 ? ETA_SSD_AFTER_MIN : ETA_HDD_AFTER_MIN);
                           hi_m = lo_m * 2.0; }
    int l = (int)(lo_m / 5.0) * 5;
    if (l < 5) l = 5;
    int h = (int)ceil(hi_m / 5.0) * 5;
    if (h > 30) h = (int)ceil(hi_m / 15.0) * 15;
    if (h <= l) h = l + 5;
    *lo = l; *hi = h;
}

/* "25 to 60 minutes", or "1 to 3 hours" when it is long */
static void range_words(int lo, int hi, wchar_t *out, int cch)
{
    if (hi >= 120) {
        int l = lo / 60 < 1 ? 1 : lo / 60, h = (hi + 59) / 60;
        if (h <= l) h = l + 1;
        _snwprintf(out, (size_t)cch, L"%d to %d hours", l, h);
    } else {
        _snwprintf(out, (size_t)cch, L"%d to %d minutes", lo, hi);
    }
    out[cch - 1] = 0;
}

static const pf_volume *sys_volume(void)
{
    if (g_report.system_volume >= 0 && g_report.system_volume < g_report.n_volumes)
        return &g_report.volumes[g_report.system_volume];
    return NULL;
}

#define GIB (1024ULL * 1024 * 1024)

/* A tick box she can untick, ticked for her. The whole row is the
 * target, at least 44 px tall. */
static int toggle_row(int id, int on, const wchar_t *title, const wchar_t *sub,
                      int x, int y, int w)
{
    int pad = S(14), box = S(26);
    int tx = x + pad + box + S(16), tw = w - (tx - x) - pad;
    int th = text_h(title, g_f_bodyb, tw, DT_WORDBREAK);
    int sh = sub ? text_h(sub, g_f_body, tw, DT_WORDBREAK) : 0;
    int h = pad * 2 + th + (sub ? sh + S(2) : 0);
    if (h < S(56)) h = S(56);
    int idx = w_add(id, W_TOGGLE, x, y, w, h, 1);
    if (idx >= 0) g_w[idx].name = title;
    int hot = w_hot(idx);
    fill_rr((float)x, (float)y, (float)w, (float)h, (float)S(12), C_SURFACE, 1.f);
    stroke_rr((float)x, (float)y, (float)w, (float)h, (float)S(12), (float)S(2),
              hot ? C_INK : C_RULE, 1.f);
    float bx = (float)(x + pad), by = (float)(y + (h - box) / 2);
    if (on) {
        fill_rr(bx, by, (float)box, (float)box, (float)S(5), C_ACCENT, 1.f);
        glyph_check(bx + box * 0.5f, by + box * 0.5f, (float)box * 0.62f, C_ON_ACCENT, 1.f);
    } else {
        stroke_rr(bx, by, (float)box, (float)box, (float)S(5), (float)S(2), C_INK, 1.f);
    }
    int ty = y + (h - th - (sub ? sh + S(2) : 0)) / 2;
    text_draw(title, g_f_bodyb, C_INK, tx, ty, tw, DT_WORDBREAK);
    if (sub) text_draw(sub, g_f_body, C_INK2, tx, ty + th + S(2), tw, DT_WORDBREAK);
    if (w_focused(idx)) { RECT b = { x, y, x + w, y + h }; focus_ring(b, S(4), S(15)); }
    return h;
}

/* READY: what stays, what it takes, what it looks like, what it
 * downloads, the one restart and the way back, and how long -- each one
 * line, before she presses anything (docs/EASY.md, rule 9). */
static int page_ready(int x, int y, int w)
{
    int y0 = y;
    int narrow = w > S(760) ? S(760) : w;
    wchar_t buf[300], hwl[420];
    wchar_t rng[64], clk[300];

    /* THE ROWS, collected first so that the space between them can be
     * chosen to fit: the first two are the Terms' risk in her words (stream
     * LEGAL, docs/handoff/streams/LEGAL.md items 1, 10, 11), and everything
     * else was here before. */
    struct { int glyph; const wchar_t *text; } row[16];
    int nr = 0;
#define ROW(g, t) do { row[nr].glyph = (g); row[nr].text = (t); nr++; } while (0)

    /* THE ONE HAZARD, before anything else. "Kept" is what AurOS is made to
     * do, not a promise: the step that makes Windows smaller cannot be undone
     * half way. */
    ROW(G_HOME, L"Windows and your files are kept. Setup makes Windows smaller to fit AurOS. "
                L"If the power fails or the computer crashes during that step, Windows or "
                L"your files could be damaged and might not come back.");
    ROW(G_DRIVE, L"Any change to a drive carries some risk. Copy anything you can’t "
                 L"replace to a memory stick first.");
    /* and the one thing that does not come with her (A-07, EASY rule 9: say
     * it before, not after) */
    /* and what of hers will not work yet, in the same row (never a stop) */
    static wchar_t nowork[480];
    wcscpy(nowork, L"Programs made for Windows don’t run in AurOS.");
    if (hw_ready_line(hwl, 420)) { wcscat(nowork, L" "); wcsncat(nowork, hwl, 420); }
    ROW(G_INFO, nowork);
    ROW(G_LOOK, STYLES[g_sel_style].ready);

    if (g_dl_state == 2) {
        wcscpy(buf, L"AurOS has already downloaded.");
    } else {
        wcscpy(buf, L"AurOS downloads while you wait, so stay connected to the internet.");
    }
    ROW(G_DOWN, buf);

    /* D11, in her words, on a computer whose drive is locked. */
    if (bitlocker_paused_here())
        ROW(G_LOCK, L"Windows’ drive lock is paused for one restart and comes back on by itself.");

    ROW(G_UNDO, L"The computer restarts once. Then AurOS asks if everything "
                L"works. Press “No, go back to Windows” and it does.");

    int lo, hi;
    eta_range(&lo, &hi);
    range_words(lo, hi, rng, 64);
    _snwprintf(clk, 299, L"It takes about %ls. The screen goes dark for a minute or "
                         L"two.%ls", rng,
               disk_is_slow() == 1
                 ? L" This computer has an older, slower kind of drive." : L"");
    clk[299] = 0;
    ROW(G_CLOCK, clk);

    /* WHAT SETUP LEAVES BEHIND, said before she agrees (TERMS 4): the Terms
     * list it and this is the short version. */
    ROW(G_INFO, L"Setup turns off Fast Startup and hibernation, and leaves a Windows task, "
                L"“AurOS put-back”, that does nothing unless AurOS is removed.");
#undef ROW

    /* the Wi-Fi box, measured as it will be drawn */
    wchar_t title[200];
    int tog_h = 0;
    if (wifi_offered()) {
        wchar_t net[128];
        MultiByteToWideChar(CP_UTF8, 0, g_report.wifi_name, -1, net, 127);
        net[127] = 0;
        if (net[0]) _snwprintf(title, 199, L"Bring my Wi-Fi across (%ls)", net);
        else        wcscpy(title, L"Bring my Wi-Fi across");
        title[199] = 0;
        int tw = narrow - S(14) - S(26) - S(16) - S(14);
        tog_h = S(14) * 2 + text_h(title, g_f_bodyb, tw, DT_WORDBREAK)
              + text_h(L"So AurOS is online the moment it starts.", g_f_body, tw, DT_WORDBREAK) + S(2);
        if (tog_h < S(56)) tog_h = S(56);
    }

    int head_gap = S(g_dense ? 12 : g_short ? 18 : 28);
    int head_h = TYPE[type_of(g_f_title)].line;
    int rows_h = 0, tx_w = narrow - S(30) - S(16);
    for (int i = 0; i < nr; i++) rows_h += text_h(row[i].text, g_f_body, tx_w, DT_WORDBREAK);
    /* the space between rows: as much as 16 px when there is room, as little as
     * 4 px when there is not (past that the page scrolls) */
    int gaps = nr - 1 + (tog_h ? 1 : 0);
    /* (render() adds 16 px under every page; the rows must leave it free) */
    int room = (g_body.bottom - g_body.top) - S(16) - head_h - head_gap - rows_h - (tog_h ? tog_h + S(4) : 0);
    if (gaps > 0 && room < gaps * S(8)) {          /* tight: the title sits closer to its rows too */
        head_gap = S(12);
        room = (g_body.bottom - g_body.top) - S(16) - head_h - head_gap - rows_h - (tog_h ? tog_h + S(4) : 0);
    }
    int gap = gaps > 0 ? room / gaps : S(16);
    if (gap > S(16)) gap = S(16);
    if (gap < S(4)) gap = S(4);

    y += page_head(L"Ready to install", NULL, x, y, w) + head_gap;
    for (int i = 0; i < nr; i++) {
        y += glyph_row(row[i].glyph, row[i].text, x, y, narrow);
        if (i < nr - 1 || tog_h) y += gap;
    }

    /* D10: her Wi-Fi, ticked for her, said by its own name. */
    if (tog_h) {
        y += toggle_row(ID_WIFI, g_bring_wifi, title,
                        L"So AurOS is online the moment it starts.", x, y, narrow);
    }
    return y - y0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  4 · PROGRESS
 *
 *  One bar, one sentence, and how long is left -- measured, not
 *  guessed, once there is enough to measure. Then "Restart now", and
 *  a restart by itself after a minute she can watch count down.
 * ═══════════════════════════════════════════════════════════════════ */

/* How much of the work before the restart each phase is. The download
 * is nearly all of it. A phase the engine adds later gets a small share
 * rather than none, so the bar never stalls at the end of one. */
static float phase_weight(int ph)
{
    switch (ph) {
    case AB_INSPECT: return 2.f;
    case AB_CONSENT: return 1.f;
    case AB_PREPARE: return 90.f;
    case AB_HANDOFF: return 7.f;
    default:         return 5.f;
    }
}

static float overall_fraction(int phase, int pct)
{
    float total = 0.f, done = 0.f;
    for (int i = 0; i < AB_N; i++) total += phase_weight(i);
    if (phase < 0) return 0.f;
    if (phase >= AB_N) return 1.f;
    for (int i = 0; i < phase; i++) done += phase_weight(i);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    done += phase_weight(phase) * (float)pct / 100.f;
    return total > 0.f ? done / total : 0.f;
}

/* A sample every few seconds; the rate is taken over the last minute
 * or so, so a stall shows and a burst does not promise too much. */
#define ETA_SAMPLES 24
static struct { ULONGLONG t; float f; } g_eta_s[ETA_SAMPLES];
static int       g_eta_n, g_eta_head;
static int       g_eta_min = -1;       /* minutes left, -1 not known yet */
static ULONGLONG g_eta_shown;

static void eta_reset(void) { g_eta_n = 0; g_eta_head = 0; g_eta_min = -1; g_eta_shown = 0; }

static void eta_sample(float f, ULONGLONG now)
{
    int last = (g_eta_head + ETA_SAMPLES - 1) % ETA_SAMPLES;
    if (g_eta_n && now - g_eta_s[last].t < 3000) return;
    g_eta_s[g_eta_head].t = now; g_eta_s[g_eta_head].f = f;
    g_eta_head = (g_eta_head + 1) % ETA_SAMPLES;
    if (g_eta_n < ETA_SAMPLES) g_eta_n++;
    int first = (g_eta_head + ETA_SAMPLES - g_eta_n) % ETA_SAMPLES;
    ULONGLONG dt = now - g_eta_s[first].t;
    float df = f - g_eta_s[first].f;
    if (dt < 10000 || df < 0.004f) return;
    double rate = (double)df / ((double)dt / 1000.0);
    double rem = (1.0 - (double)f) / rate / 60.0 + after_restart_min();
    int m = (int)ceil(rem);
    if (g_eta_min < 0 || now - g_eta_shown > 5000) { g_eta_min = m; g_eta_shown = now; }
}

static void eta_words(int m, wchar_t *out, int cch)
{
    if (m <= 1)       wcscpy(out, L"About a minute left");
    else if (m < 10)  _snwprintf(out, (size_t)cch, L"About %d minutes left", m);
    else if (m < 60)  _snwprintf(out, (size_t)cch, L"About %d minutes left", ((m + 4) / 5) * 5);
    else              wcscpy(out, L"More than an hour left");
    out[cch - 1] = 0;
}

static const wchar_t *stage_sentence(int phase)
{
    switch (phase) {
    case AB_INSPECT: return L"Checking this computer once more.";
    case AB_CONSENT: return L"Getting ready.";
    case AB_PREPARE: return L"Downloading AurOS, and checking every piece of it.";
    case AB_HANDOFF: return L"Getting the restart ready.";
    default:         return L"Setting up AurOS.";
    }
}

/* THE BAR NEVER GOES BACKWARDS. The early download (download_begin)
 * reports through the same callbacks before the engine's phases start,
 * and the engine then starts again at its first phase: shown as it is,
 * the bar would fall back to nothing just after the longest wait. */
static float g_bar_f;

static int early_download_running(void) { return g_dl_state == 1; }

static int install_ok(void)     { return g_install_finished && g_ab_state == 2; }
static int install_failed(void) { return g_install_finished && g_ab_state == 3; }

static void read_engine(int *phase, int *pct)
{
    *phase = 0; *pct = 0;
    if (!g_ab_lock_ready) return;
    EnterCriticalSection(&g_ab_lock);
    *phase = g_ab_phase; *pct = g_ab_pct;
    LeaveCriticalSection(&g_ab_lock);
}

/* The step the engine is on, as she counts them: 1 to 4 before the
 * restart. */
#define N_STEPS 4
static int step_of(int phase)
{
    if (phase < AB_INSPECT) phase = AB_INSPECT;
    if (phase > AB_HANDOFF) phase = AB_HANDOFF;
    return phase - AB_INSPECT + 1;
}

static int page_progress(int x, int y, int w)
{
    int y0 = y;
    int narrow = w > S(760) ? S(760) : w;
    wchar_t buf[520];

    if (install_ok()) {
        y += page_head(L"Ready to restart",
                       L"Save anything you\u2019re working on, then press Restart now.",
                       x, y, w) + S(g_short ? 20 : 32);
        static const wchar_t *const what[] = {
            L"Restart now: the screen goes black for a minute or two. Then you see "
            L"\u201CGetting ready\u201D and \u201CInstalling AurOS\u201D. Do not turn the "
            L"computer off. It restarts by itself, and AurOS asks if everything works.",
            L"Restart later: this window waits. Setup has already turned off Fast Startup and "
            L"put AurOS\u2019s start-up files on this computer. Nothing restarts until you come "
            L"back and press Restart now.",
        };
        y += note_box(NULL, what, 2, x, y, narrow);
        return y - y0;
    }

    if (install_failed()) {
        wchar_t why[400];
        MultiByteToWideChar(CP_UTF8, 0, g_ab_why[0] ? g_ab_why : "it stopped", -1, why, 399);
        why[399] = 0;
        _snwprintf(buf, 519, L"Setup stopped. If Windows does not start, ask someone to help "
                             L"you. %ls", plain_failure(why));
        buf[519] = 0;
        y += page_head(L"The install stopped", buf, x, y, w);
        return y - y0;
    }

    int phase, pct;
    read_engine(&phase, &pct);
    if (phase < 0) phase = 0;
    if (early_download_running() && phase < AB_PREPARE) phase = AB_PREPARE;
    float f = overall_fraction(phase, pct);
    if (f > g_bar_f) g_bar_f = f;
    f = g_bar_f;

    y += page_head(L"Setting up AurOS", NULL, x, y, w) + S(g_short ? 18 : 28);
    if (g_stop_wanted) {
        y += text_draw(L"Stopping.", g_f_bodyb, C_INK, x, y, narrow, DT_WORDBREAK) + S(4);
        y += text_draw(L"When the step it is on is done, everything is put back and this "
                       L"window closes by itself.",
                       g_f_body, C_INK, x, y, narrow, DT_WORDBREAK);
        return y - y0;
    }
    _snwprintf(buf, 519, L"Part %d of %d: %ls", step_of(phase), N_STEPS, stage_sentence(phase));
    buf[519] = 0;
    y += text_draw(buf, g_f_bodyb, C_INK, x, y, narrow, DT_WORDBREAK) + S(14);

    /* THE BAR: determinate once there is anything to show, the stepping
     * bar before. Under 30 px tall at any scale the e2e driver runs at,
     * so it is never taken for a button. */
    int bh = S(12);
    if (f > 0.f) {
        fill_rr((float)x, (float)y, (float)narrow, (float)bh, (float)bh * 0.5f, C_SOFT, 1.f);
        stroke_rr((float)x, (float)y, (float)narrow, (float)bh, (float)bh * 0.5f, 1.f, C_RULE, 1.f);
        float fw = (float)narrow * clampf(f, 0.f, 1.f);
        if (fw > (float)bh) fill_rr((float)x, (float)y, fw, (float)bh, (float)bh * 0.5f,
                                    C_ACCENT, 1.f);
    } else {
        stepper(x, y, narrow, bh);
    }
    y += bh + S(12);

    if (g_eta_min >= 0) eta_words(g_eta_min, buf, 520);
    else wcscpy(buf, L"Working out how long this takes.");
    y += text_draw(buf, g_f_body, C_INK2, x, y, narrow, DT_SINGLELINE) + S(g_short ? 20 : 32);

    static const wchar_t *const away[] = {
        L"You can walk away. When you come back, press Restart now.",
        L"If the screen has gone dark, move the mouse.",
    };
    y += note_box(NULL, away, 2, x, y, narrow);
    return y - y0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Footer, and what the primary button means on each page
 * ═══════════════════════════════════════════════════════════════════ */
static int welcome_passed(void)
{
    return g_pf_valid && !g_checking && pf_is_go(&g_report);
}

static int primary_visible(void)
{
    if (g_confirm || g_pane) return 1;
    if (g_page == PAGE_PROGRESS) return g_install_finished;
    return 1;
}

static int primary_enabled(void)
{
    if (g_confirm || g_pane) return 1;
    switch (g_page) {
    case PAGE_WELCOME:  return welcome_passed();
    case PAGE_BLOCKED:  return sb_page() ? !g_checking
                             : any_cannot() || (!g_fix_busy && !g_checking);
    case PAGE_STYLE:    return nav_allowed(PAGE_READY);
    case PAGE_READY:    return !g_checking && nav_allowed(PAGE_PROGRESS);
    case PAGE_PROGRESS: return g_install_finished && !g_checking;
    default:            return 0;
    }
}

static const wchar_t *primary_label(void)
{
    if (g_confirm) return g_confirm == 1 ? L"Keep going"
                        : g_confirm == 3 ? L"I have saved my work" : L"Keep this window open";
    if (g_pane) return L"Done reading";
    switch (g_page) {
    case PAGE_WELCOME:  return L"Next";
    /* WHEN IT CANNOT GO ON, THE ONE BUTTON CLOSES IT. "Check again" on an
     * ARM laptop or a BIOS computer is a loop with a green button on it.
     * The page still checks by itself, in case it was wrong. The one
     * setting a helper can change has its own page (sb_page). */
    case PAGE_BLOCKED:  return sb_page() ? (g_sb_helper ? L"Check again" : L"Show a helper how")
                             : any_cannot() ? L"Close AurOS Setup"
                             : g_checking ? L"Checking…"
                             : g_fix_busy ? L"Fixing…"
                             : fixable_now() ? L"Fix these for me" : L"Check again";
    case PAGE_STYLE:    return L"Next";
    case PAGE_READY:    return g_checking ? L"Checking…" : L"Install AurOS";
    case PAGE_PROGRESS: return install_failed() ? (g_checking ? L"Checking…"
                                                              : L"Try again")
                                                : L"Restart now";
    default:            return L"Next";
    }
}

static int back_visible(void)
{
    /* Not while "Install AurOS" is checking: that button was pressed. */
    if (g_pane) return 0;
    return g_page == PAGE_STYLE || (g_page == PAGE_READY && !g_checking);
}
static page_id back_target(void)
{
    switch (g_page) {
    case PAGE_STYLE: return PAGE_WELCOME;
    case PAGE_READY: return PAGE_STYLE;
    default:         return g_page;
    }
}

/* WHY THE BIG BUTTON DID NOTHING, said beside it when she presses it
 * too early (DESIGN-DIRECTION.md §3: the primary button is always there,
 * and explains itself). g_early is cleared when the page changes. */

static const wchar_t *early_words(void)
{
    switch (g_page) {
    case PAGE_WELCOME:  return L"Still looking at this computer.";
    case PAGE_BLOCKED:  return g_fix_busy ? L"Still fixing." : L"Still checking.";
    case PAGE_STYLE:    return L"Pick a look first.";
    case PAGE_READY:    return L"Checking this computer once more.";
    default:            return L"Not yet.";
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  The Terms and the Licenses, read inside the window
 *
 *  "By pressing Install AurOS you agree to the AurOS Terms" is only fair
 *  if she can read them, with the internet down, without a browser: the
 *  Terms are RCDATA 202 inside this file, and the licences and notices of
 *  everything AurOS carries (with the written offer of the source) are
 *  RCDATA 200 (build/aurbridge stages both from docs/legal/notices/;
 *  stream LEGAL, docs/handoff/streams/LEGAL.md). A pane replaces the page
 *  until she presses "Done reading" or Esc; nothing behind it is
 *  reachable, and it changes nothing. The text is third parties' words, so
 *  it is not held to the installer's own voice rules (wordlint reads the
 *  string literals above, not this file's resources).
 * ═══════════════════════════════════════════════════════════════════ */
static int g_want_focus_id;
#define RES_NOTICES 200
#define RES_TERMS   202
static int      g_pane_opener;          /* the button that opened it, to give focus back */
static wchar_t *g_pt;                   /* the text, reflowed: a paragraph ends in '\n' */
static int      g_pt_kind;
typedef struct { int off, len, ind; } pane_line;   /* ind: pixels, for a hanging indent */
static pane_line *g_pl;
static int      g_pl_n, g_pl_cap, g_pl_w = -1;
static HFONT    g_pl_font;

/* RCDATA `id` as wide text; NULL when this file has no such resource. */
static wchar_t *res_wide(int id)
{
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    if (!r) return NULL;
    HGLOBAL g = LoadResource(NULL, r);
    DWORD n = SizeofResource(NULL, r);
    const char *b = g ? (const char *)LockResource(g) : NULL;
    if (!b || !n) return NULL;
    if (n >= 3 && (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB && (unsigned char)b[2] == 0xBF) {
        b += 3; n -= 3;
    }
    int w = MultiByteToWideChar(CP_UTF8, 0, b, (int)n, NULL, 0);
    if (w <= 0) return NULL;
    wchar_t *o = (wchar_t *)malloc(((size_t)w + 1) * sizeof *o);
    if (!o) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, b, (int)n, o, w);
    o[w] = 0;
    return o;
}

/* Hard-wrapped plain text becomes paragraphs: a flush-left (or equally
 * indented) line that follows a line of 55 characters or more continues it;
 * a short line, a blank, a list item with a different indent, or a rule
 * starts a new one. "=====" frames become a thin rule (0x01), "-----"
 * underlines go. The licences are hard-wrapped at about 75 columns, which
 * would otherwise wrap again here into a short line after every long one. */
static void pane_reflow(const wchar_t *raw)
{
    size_t n = wcslen(raw), tabs = 0;
    for (size_t i = 0; i < n; i++) if (raw[i] == L'\t') tabs++;
    wchar_t *t = (wchar_t *)malloc((n + tabs * 3 + 2) * sizeof *t), *tw = t;
    for (size_t i = 0; i < n; i++) {
        if (raw[i] == L'\t') { for (int k = 0; k < 4; k++) *tw++ = L' '; }
        else if (raw[i] == L'\r') continue;
        else if (raw[i] == 0x0C) *tw++ = L'\n';
        else *tw++ = raw[i];
    }
    *tw = 0;
    wchar_t *o = (wchar_t *)malloc((n + 16) * sizeof *o), *w = o;
    int open = 0, prev_indent = -1, prev_len = 0, blanks = 0;
    const wchar_t *p = t;
    while (*p) {
        const wchar_t *a = p, *e = p;
        while (*e && *e != L'\n') e++;
        p = *e ? e + 1 : e;
        const wchar_t *b = e;
        while (b > a && b[-1] == L' ') b--;
        int len = (int)(b - a);
        if (len == 0) {
            if (open) { *w++ = L'\n'; open = 0; }
            if (blanks < 1) *w++ = L'\n';
            blanks++; prev_indent = -1; prev_len = 0;
            continue;
        }
        blanks = 0;
        int rule = len >= 3;
        for (const wchar_t *c = a; rule && c < b; c++) if (!wcschr(L"=-_~*", *c)) rule = 0;
        if (rule) {
            if (open) { *w++ = L'\n'; open = 0; }
            if (a[0] == L'=' || a[0] == L'_' || a[0] == L'~' || a[0] == L'*') { *w++ = 1; *w++ = L'\n'; }
            prev_indent = -1; prev_len = 0;
            continue;
        }
        int ind = 0;
        while (a + ind < b && a[ind] == L' ') ind++;
        if (open && ind == prev_indent && prev_len >= 55) {
            *w++ = L' ';
            for (const wchar_t *c = a + ind; c < b; c++) *w++ = *c;
        } else {
            if (open) *w++ = L'\n';
            for (const wchar_t *c = a; c < b; c++) *w++ = *c;
            open = 1;
        }
        prev_indent = ind; prev_len = len;
    }
    if (open) *w++ = L'\n';
    *w = 0;
    free(t);
    free(g_pt);
    g_pt = o;
}

static void pane_load(void)
{
    if (g_pt && g_pt_kind == g_pane) return;
    wchar_t *raw = res_wide(g_pane == 2 ? RES_NOTICES : RES_TERMS);
    if (!raw) {
        raw = _wcsdup(L"This copy of Setup does not carry the text. It is on the AurOS website, "
                      L"on the Licenses page, and in every AurOS release.");
    }
    if (raw) { pane_reflow(raw); free(raw); }
    g_pt_kind = g_pane;
    g_pl_w = -1;
}

static void pane_add(int off, int len, int ind)
{
    if (g_pl_n >= g_pl_cap) {
        int nc = g_pl_cap ? g_pl_cap * 2 : 4096;
        pane_line *np = (pane_line *)realloc(g_pl, (size_t)nc * sizeof *np);
        if (!np) return;
        g_pl = np; g_pl_cap = nc;
    }
    g_pl[g_pl_n].off = off; g_pl[g_pl_n].len = len; g_pl[g_pl_n].ind = ind;
    g_pl_n++;
}

/* Break the paragraphs into lines that fit `width`, at a space; the first line
 * keeps its own leading spaces, the rest hang two spaces deeper. */
static void pane_wrap(int width)
{
    if (!g_pt) return;
    if (g_pl_w == width && g_pl_font == g_f_body) return;
    g_pl_w = width; g_pl_font = g_f_body; g_pl_n = 0;
    SelectObject(g_mdc, g_f_body);
    SIZE sp = { 0, 0 };
    GetTextExtentPoint32W(g_mdc, L" ", 1, &sp);
    const wchar_t *base = g_pt, *q = g_pt;
    while (*q) {
        const wchar_t *s = q, *e = q;
        while (*e && *e != L'\n') e++;
        q = *e ? e + 1 : e;
        int len = (int)(e - s);
        if (len == 0 || s[0] == 1) { pane_add((int)(s - base), len, 0); continue; }
        int lead = 0;
        while (lead < len && s[lead] == L' ') lead++;
        int hang = lead ? (lead + 2) * sp.cx : 0;      /* a list item hangs; a paragraph does not */
        if (hang > width / 3) hang = width / 3;
        const wchar_t *c = s;
        int first = 1;
        while (c < e) {
            int avail = width - (first ? 0 : hang), rem = (int)(e - c), try_ = rem > 300 ? 300 : rem, fit = 0;
            SIZE sz = { 0, 0 };
            GetTextExtentExPointW(g_mdc, c, try_, avail, &fit, NULL, &sz);
            if (fit >= rem) { pane_add((int)(c - base), rem, first ? 0 : hang); break; }
            int brk = 0;
            for (int i = fit; i > 0; i--)
                if (c[i] == L' ' && c[i - 1] != L' ') { brk = i; break; }
            if (brk <= 0) brk = fit > 0 ? fit : 1;
            pane_add((int)(c - base), brk, first ? 0 : hang);
            c += brk;
            while (c < e && *c == L' ') c++;
            first = 0;
        }
    }
}

static void pane_open(int kind, int opener)
{
    g_pane = kind;
    g_pane_opener = opener;
    g_scroll[PAGE_COUNT] = 0;
    g_focus = -1;
    g_focus_ring = 0;
    page_changed();
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

static void pane_close(void)
{
    g_pane = 0;
    page_changed();                      /* a double-click on "Done reading" must not press what is under it */
    g_want_focus_id = g_pane_opener;
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

static const wchar_t *pane_title(void)
{
    return g_pane == 2 ? L"Licenses and notices" : L"The AurOS Terms";
}

static int page_pane(int x, int y, int w)
{
    int y0 = y;
    pane_load();
    y += page_head(pane_title(), NULL, x, y, w);
    y += text_draw(L"You can read this without the internet. Scroll with the mouse wheel or "
                   L"the arrow keys.", g_f_small, C_INK2, x, y + S(4),
                   w > S(780) ? S(780) : w, DT_WORDBREAK) + S(g_short ? 12 : 20);
    int tw = w > S(780) ? S(780) : w;
    pane_wrap(tw);
    int lh = TYPE[type_of(g_f_body)].line, off = (lh - TYPE[type_of(g_f_body)].tm) / 2;
    int first = (g_body.top - y) / lh - 1, last = (g_body.bottom - y) / lh + 2;
    if (first < 0) first = 0;
    if (last > g_pl_n) last = g_pl_n;
    SelectObject(g_mdc, g_f_body);
    SetTextColor(g_mdc, CR(C_INK));
    SetBkMode(g_mdc, TRANSPARENT);
    for (int i = first; i < last; i++) {
        const pane_line *l = &g_pl[i];
        int ly = y + i * lh;
        if (l->len == 1 && g_pt[l->off] == 1)
            aa_line((float)x, (float)(ly + lh / 2), (float)(x + tw), (float)(ly + lh / 2),
                    (float)S(1), C_RULE, 1.f);
        else if (l->len > 0)
            TextOutW(g_mdc, x + l->ind, ly + off, g_pt + l->off, l->len);
    }
    return y - y0 + g_pl_n * lh;
}

/* A LINK THAT IS A BUTTON: ink words, always underlined (2 px, 3 px under the
 * pointer), no box. Ink and not the green, which is the colour of the one big
 * button and nothing else. */
static int draw_link(int id, const wchar_t *label, HFONT f, int x, int y, int w, int h)
{
    int idx = w_add(id, W_BUTTON, x, y, w, h, 1);
    if (idx >= 0) g_w[idx].name = label;
    int hot = w_hot(idx);
    int down = hot && g_mouse_down && g_press_idx == idx;
    RECT box = { x, y, x + w, y + h };
    text_in(label, f, down ? C_INK2 : C_INK, box, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
    int tw = text_w(label, f), th = TYPE[type_of(f)].px;
    int uy = y + (h + th) / 2 + S(2);
    fill_rr((float)x, (float)uy, (float)tw, (float)S(hot ? 3 : 2), 0.f, hot ? C_INK : C_INK2, 1.f);
    if (w_focused(idx)) focus_ring((RECT){ x - S(2), y, x + w, y + h }, S(2), S(6));
    return idx;
}

/* THE AGREEMENT, directly above Install AurOS (TERMS 4, check.py --launch):
 * the sentence at the body's size and colour, and the way to read what it
 * means. On one row when they fit side by side, else two. */
static const wchar_t AGREE_LINE[] = L"By pressing Install AurOS you agree to the AurOS Terms.";
static const wchar_t AGREE_LINK[] = L"Read the Terms";
static int agree_shown(void) { return g_page == PAGE_READY && !g_confirm && !g_pane; }
static int agree_rows(void)
{
    int need = text_w(AGREE_LINE, g_f_body) + S(24) + text_w(AGREE_LINK, g_f_body);
    return need <= g_foot.right - g_foot.left ? 1 : 2;
}
static int agree_h(void)
{
    return agree_rows() * TYPE[type_of(g_f_body)].line + S(8);
}

/* THE BUTTONS, bottom right: the primary at the page's right edge, the
 * second one left of it -- 32 px away when it leaves or undoes something.
 * At the left, why the big button is waiting, or where "Not now" leaves
 * her. Install AurOS is wider than Next, carries a check mark, and says
 * above itself what it does, so it never passes for the Next that stood
 * there a page ago (B-2). */
static void draw_footer(void)
{
    int bh = S(56);
    int by = g_foot.top;
    int bx = g_foot.right;

    if (primary_visible()) {
        const wchar_t *plab = primary_label();
        int install = g_page == PAGE_READY && !g_checking && !g_pane && !g_confirm;
        int pw = text_w(plab, g_f_btn) + S(64) + (install ? S(30) : 0);
        int min = install ? S(280) : S(200);
        if (pw < min) pw = min;
        bx -= pw;
        draw_button(ID_PRIMARY, plab, bx, by, pw, bh, 1, install);
        if (agree_shown()) {
            /* the sentence, and the way to read what it means, right-aligned
             * above the button; one row when they fit side by side */
            int lh = TYPE[type_of(g_f_body)].line, rows = agree_rows();
            int ly = by - S(8) - lh;                       /* the bottom row */
            int lw = text_w(AGREE_LINK, g_f_body);
            RECT a = { g_foot.left, ly - (rows - 1) * lh, g_foot.right, ly - (rows - 1) * lh + lh };
            if (rows == 1) a.right = g_foot.right - lw - S(24);
            text_in(AGREE_LINE, g_f_body, C_INK, a, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
            int th = lh < S(44) ? S(44) : lh;               /* a target at least 44 px tall */
            int ty = ly + (lh - th) / 2;
            if (ty + th > by) ty = by - th;                 /* never over the button */
            draw_link(ID_TERMS, AGREE_LINK, g_f_body, g_foot.right - lw, ty, lw, th);
        }
    }

    /* the buttons that are not the big one, nearest to it first */
    struct { const wchar_t *label; int id, leaves; } sec[3];
    int ns = 0;
    const wchar_t *lands = NULL;
#define SEC(l, i, lv) do { sec[ns].label = (l); sec[ns].id = (i); sec[ns].leaves = (lv); ns++; } while (0)
    if (g_pane && !g_confirm) {
        /* reading the Terms or the Licenses: one button, back to the page */
    } else if (g_confirm == 3) {
        SEC(L"Not yet", ID_LEAVE, 1);
        lands = L"Not yet: nothing is changed, and the page stays as it is.";
    } else if (g_confirm) {
        SEC(g_confirm == 1 ? L"Stop and undo" : L"Close and undo", ID_LEAVE, 1);
        lands = L"Undo: this computer starts Windows as usual.";
    } else if (g_page == PAGE_PROGRESS && g_install_running && g_ab_gate == 0 && !g_stop_wanted) {
        /* Stop is offered only while stopping is safe: AurOS has only been
         * downloading, and nothing on the computer has been changed */
        SEC(L"Stop", ID_STOP, 1);
        lands = L"Nothing has been changed yet.";
    } else if (g_page == PAGE_BLOCKED && (sb_page() || !any_cannot())) {
        SEC(L"Not now", ID_QUIT, 1);
        if (!sb_page() && helper_url()) SEC(L"Show a helper how", ID_HELPER, 0);   /* the USB stick list */
        lands = L"Not now: AurOS Setup closes. Nothing has been changed.";
    } else if (g_page == PAGE_BLOCKED) {
        /* it cannot go on: what a helper needs, and where to read it */
        if (helper_url()) SEC(L"Show a helper how", ID_HELPER, 0);
        SEC(L"Save a report for a helper", ID_REPORT, 0);
    } else if (back_visible()) {
        SEC(L"Back", ID_BACK, 0);
    } else if (g_page == PAGE_PROGRESS && install_ok()) {
        SEC(L"Restart later", ID_LATER, 1);
    } else if (g_page == PAGE_PROGRESS && install_failed()) {
        SEC(L"Not now", ID_QUIT, 1);
        lands = L"Not now: AurOS Setup closes.";
    }
    if (g_page == PAGE_BLOCKED && g_rep_state == 1) lands = L"Saving the report\u2026";
    else if (g_page == PAGE_BLOCKED && g_rep_state == 2)
        lands = L"Saved on your Desktop: AurOS-report.txt. Give it to your helper.";
    else if (g_page == PAGE_BLOCKED && g_rep_state == 3) lands = L"The report could not be saved.";
    for (int i = 0; i < ns; i++) {
        int sw = text_w(sec[i].label, g_f_btn) + S(56);
        if (sw < S(140)) sw = S(140);
        /* flush right when the big button is not there; a gap of 32 px
         * before a button that leaves or undoes, 16 before one that does not */
        bx -= sw + ((i == 0 && !primary_visible()) ? 0 : S(i == 0 && sec[i].leaves ? 32 : 16));
        draw_button(sec[i].id, sec[i].label, bx, by, sw, bh, 0, 0);
    }
#undef SEC

    /* "Licenses", at the left of every page's foot: the licences of everything
     * AurOS carries, one press away and readable offline (LIC-3). What the
     * foot says about the buttons starts after it. */
    int left = g_foot.left;
    if (!g_pane && !g_confirm) {
        const wchar_t *lic = L"Licenses";
        int lw = text_w(lic, g_f_small), lhh = S(44);
        draw_link(ID_LICENSES, lic, g_f_small, left, by + (bh - lhh) / 2, lw + S(4), lhh);
        left += lw + S(32);
    }
    const wchar_t *say = g_early ? g_early : lands;
    int sw = bx - S(24) - left;
    if (say && sw > S(220)) {          /* too narrow to read: the page says it */
        int th = text_h(say, g_f_small, sw, DT_WORDBREAK);
        text_draw(say, g_f_small, C_INK2, left, by + (bh - th) / 2, sw, DT_WORDBREAK);
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  Render
 * ═══════════════════════════════════════════════════════════════════ */
static int g_want_focus_id;
static int g_hot_idx = -1;
static int g_settle;

/* The question, in her words, with where each answer lands. The big
 * button keeps what she has; the other one undoes it. */
static int page_confirm(int x, int y, int w)
{
    int y0 = y;
    if (g_confirm == 1) {
        y += page_head(L"Stop setting up AurOS?",
                       L"It stops when the step it is on is done, then takes everything "
                       L"back, so this computer starts Windows as usual.", x, y, w);
    } else if (g_confirm == 3) {
        /* before Windows restarts for a fix (C-01): her work first */
        y += page_head(L"Save your work first",
                       L"Fixing this restarts Windows. Save anything you have open now. "
                       L"AurOS Setup opens again by itself afterwards.", x, y, w);
    } else {
        y += page_head(L"Close AurOS Setup?",
                       L"AurOS is ready. If you close this now, AurOS is taken back and this "
                       L"computer starts Windows as usual. You can open AurOS Setup again "
                       L"later.", x, y, w);
    }
    return y - y0;
}

static int page_dispatch(int x, int y, int w)
{
    if (g_confirm) return page_confirm(x, y, w);    /* a question about closing wins over the pane */
    if (g_pane) return page_pane(x, y, w);
    switch (g_page) {
    case PAGE_WELCOME:  return page_welcome(x, y, w);
    case PAGE_BLOCKED:  return page_blocked(x, y, w);
    case PAGE_STYLE:    return page_style(x, y, w);
    case PAGE_READY:    return page_ready(x, y, w);
    case PAGE_PROGRESS: return page_progress(x, y, w);
    default:            return 0;
    }
}

static int scroll_max(void)
{
    int m = g_content_h - g_view_h;
    return m > 0 ? m : 0;
}

static void acc_after_render(void);

static void render(void)
{
    if (!g_px) return;
    layout();
    clip_reset();
    ground();
    draw_steps();

    g_nw = 0;
    if (g_scroll[scr_i()] > scroll_max()) g_scroll[scr_i()] = scroll_max();
    set_clip(g_card.left, g_body.top - S(4), g_card.right, g_body.bottom + S(4));
    g_view_h    = g_body.bottom - g_body.top;
    g_content_h = page_dispatch(g_body.left, g_body.top - g_scroll[scr_i()],
                                g_body.right - g_body.left) + S(16);
    clip_reset();

    if (g_content_h > g_view_h) {
        /* a rule where the page goes on past the edge, instead of text
         * sheared off at it */
        if (g_scroll[scr_i()] > 0)
            aa_line((float)g_body.left, (float)(g_body.top - S(4)), (float)g_body.right,
                    (float)(g_body.top - S(4)), (float)S(2), C_RULE, 1.f);
        if (g_scroll[scr_i()] < scroll_max())
            aa_line((float)g_body.left, (float)(g_body.bottom + S(4)), (float)g_body.right,
                    (float)(g_body.bottom + S(4)), (float)S(2), C_RULE, 1.f);

        int tx = g_card.right - S(14);
        int th = S(10);
        float frac = (float)g_view_h / (float)g_content_h;
        int hh = (int)((float)(g_body.bottom - g_body.top) * frac);
        if (hh < S(44)) hh = S(44);
        int room = (g_body.bottom - g_body.top) - hh;
        int off  = scroll_max() ? (int)((float)room * (float)g_scroll[scr_i()]
                                        / (float)scroll_max()) : 0;
        fill_rr((float)tx, (float)g_body.top, (float)th,
                (float)(g_body.bottom - g_body.top), (float)th * 0.5f,
                C_RULE, 0.6f);
        fill_rr((float)tx, (float)(g_body.top + off), (float)th, (float)hh,
                (float)th * 0.5f, C_INK2, 0.9f);
    }

    draw_footer();

    /* The big button now does something else (Next became enabled,
     * "Fix these for me" became "Check again"): that is a new page too. */
    {
        static wchar_t last[64];
        const wchar_t *pl = primary_visible() && primary_enabled() ? primary_label() : L"";
        if (wcsncmp(last, pl, 63)) { wcsncpy(last, pl, 63); last[63] = 0; page_changed(); }
    }

    if (g_want_focus_id) {
        for (int i = 0; i < g_nw; i++)
            if (g_w[i].id == g_want_focus_id && g_w[i].enabled) { g_focus = i; break; }
        g_want_focus_id = 0;
    }
    if (g_focus >= g_nw) g_focus = -1;
    GdiFlush();
    acc_after_render();
}

/* ═══════════════════════════════════════════════════════════════════
 *  Actions
 * ═══════════════════════════════════════════════════════════════════ */

/* A check, run in the background. Where it goes when it comes back
 * clean is `next`: the style page from the first screen, or the
 * install itself from "Install AurOS" -- which re-runs every check
 * before anything starts (AURBRIDGE.md: destructive work re-runs
 * preflight and aborts on any block, however long the pages took). */
static void start_check(page_id next)
{
    g_check_next = next;
    g_settle = 0;
    pf_start();
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

/* The check came back clean on the first screen. THE FAST PATH'S EARLY
 * DOWNLOAD STARTS HERE (docs/handoff/SESSION-2026-10-23.md, D1): from
 * this moment until she presses Install AurOS, the download can run
 * behind the pages. Called once per clean result. */
static void check_passed(void)
{
}

static void check_advance(void)
{
    if (!g_pf_valid || g_checking) return;
    if (g_report.n_block > 0) { goto_page(PAGE_BLOCKED); return; }
    if (g_check_next == PAGE_PROGRESS) {
        /* Only from the page whose button asked for it. She may have gone
         * Back while the check ran; a clean answer then starts nothing. */
        if (g_page != PAGE_READY && g_page != PAGE_PROGRESS) {
            g_check_next = PAGE_STYLE;
            return;
        }
        /* Re-ran preflight and it is still clean: this is the only place
         * the phase list is allowed to start. */
        if (!nav_allowed(PAGE_PROGRESS)) { goto_page(PAGE_READY); return; }
        if (g_nav_mode) { g_page = PAGE_PROGRESS; return; }   /* --navtest: no engine */
        eta_reset();
        g_bar_f = 0.f;
        install_begin();
        goto_page(PAGE_PROGRESS);
        return;
    }
    /* Clean: the first screen says so, with Next. From the stop page
     * too, when what stopped it has been sorted out. */
    if (g_page != PAGE_WELCOME) goto_page(PAGE_WELCOME);
    check_passed();
    /* THE CHECK PASSED. Start fetching the image now, in the background,
     * so the five-gigabyte download runs while she reads the pages
     * instead of after. It writes only \AurOS\ (non-destructive,
     * resumable), and install_begin's worker waits for it before doing
     * anything else. Nothing has been consented to yet, and nothing
     * destructive happens here. */
    /* Not from a harness: --navtest, --shot and --selftest only render
     * and report, and a release build has an address to fetch from. */
    if (!g_nav_mode && !g_shot_mode && !g_selftest) download_begin();
    g_want_focus_id = ID_PRIMARY;      /* Next is now the obvious move */
}

/* HER PRESS ON RESTART NOW: arm the next start (the drive lock paused
 * for one restart, BootNext), then restart Windows. If either fails, what
 * was armed is taken back and she is told in one plain sentence; nothing
 * is left armed under a computer that did not restart. */
static void do_restart(void)
{
    if (g_nav_mode || g_shot_mode) { g_nt_restarts++; return; }
    char why[PLAT_WHY] = "";
    g_restarting = 1;
    if (ab_arm(&g_ab_machine, ab_say_cb, NULL, why, sizeof why) != 0 ||
        plat_restart(why, sizeof why) != 0) {
        ab_disarm(&g_ab_machine, ab_say_cb, NULL);
        g_restarting = 0;
        MessageBoxW(g_hwnd, L"The computer would not restart.\n\nNothing has been "
                            L"changed: it starts Windows as usual. Close any program "
                            L"that is asking to be saved, then press Restart now again.",
                    L"AurOS Setup", MB_OK | MB_ICONWARNING);
    }
}

static void select_style(int i)
{
    if (i < 0) i = 0;
    if (i >= N_STYLES) i = N_STYLES - 1;
    g_sel_style = i;
}

static void do_primary(void)
{
    if (!primary_visible()) return;
    if (!primary_enabled()) { g_early = early_words(); return; }
    if (g_pane && !g_confirm) { pane_close(); return; }
    if (g_confirm) {
        /* the answer that keeps what she has: keep going, or keep AurOS for
         * her next restart and let the window go */
        int c = g_confirm;
        g_confirm = 0;
        page_changed();
        if (c == 3) fix_start();               /* she saved her work: restart for the fix */
        return;
    }
    switch (g_page) {
    case PAGE_WELCOME:  goto_page(PAGE_STYLE); g_want_focus_id = ID_PRIMARY; break;
    case PAGE_BLOCKED:  if (sb_page()) {
                            if (!g_sb_helper) { g_sb_helper = 1; page_changed(); }
                            else start_check(PAGE_STYLE);
                        }
                        else if (any_cannot()) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
                        /* a fix that restarts Windows asks first, plainly: her
                         * work is lost if she has not saved it (C-01) */
                        else if (fixable_now() == 2) { g_confirm = 3; page_changed(); }
                        else if (fixable_now()) fix_start();
                        else start_check(PAGE_STYLE);
                        break;
    case PAGE_STYLE:    goto_page(PAGE_READY); g_want_focus_id = ID_PRIMARY; break;
    case PAGE_READY:    start_check(PAGE_PROGRESS); break;
    case PAGE_PROGRESS:
        if (install_ok()) { do_restart(); break; }
        if (install_failed()) {
            /* As if she had closed it and opened it again: what the
             * stopped attempt did is taken back, then a fresh check,
             * then the install. */
            install_cancel();
            g_install_finished = 0;
            start_check(PAGE_PROGRESS);
        }
        break;
    default: break;
    }
}

static void widget_do(int id)
{
    switch (id) {
    case ID_PRIMARY: do_primary(); return;
    case ID_BACK:    goto_page(back_target()); return;
    case ID_QUIT:    PostMessageW(g_hwnd, WM_CLOSE, 0, 0); return;
    /* AurOS stays armed for the next restart she makes: the window goes,
     * and nothing is taken back (WM_DESTROY). */
    /* a window that waits: nothing is armed, so nothing happens until she
     * comes back and presses Restart now */
    case ID_LATER:   g_nt_waits++; if (g_hwnd) ShowWindow(g_hwnd, SW_MINIMIZE); return;
    case ID_WIFI:    g_bring_wifi = !g_bring_wifi; return;
    case ID_HELPER:  open_helper(); return;
    case ID_REPORT:  start_report(); return;
    case ID_TERMS:    pane_open(1, ID_TERMS); return;
    case ID_LICENSES: pane_open(2, ID_LICENSES); return;
    case ID_STOP:    PostMessageW(g_hwnd, WM_CLOSE, 0, 0); return;
    case ID_LEAVE:   if (leave_confirmed()) DestroyWindow(g_hwnd); return;
    case ID_COPY:    copy_for_helper(); return;
    default: break;
    }
    if (id >= ID_STYLE && id < ID_STYLE + N_STYLES) { select_style(id - ID_STYLE); return; }
}

/* Every press she makes comes through here (or the keyboard's, below). */
static void widget_activate(int id)
{
    if (press_allowed(id)) widget_do(id);
}

/* The part of the screen a window may use (without the taskbar), on the
 * monitor `h` is on, or the main one before there is a window. */
static int work_area(HWND h, RECT *wa)
{
    HMONITOR mon = h ? MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST)
                     : MonitorFromPoint((POINT){ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    if (mon && GetMonitorInfoW(mon, &mi)) { *wa = mi.rcWork; return 1; }
    return SystemParametersInfoW(SPI_GETWORKAREA, 0, wa, 0) ? 1 : 0;
}

/* The scroll bar: where its track is, and whether it is being dragged.
 * A bar that only moved with the wheel was a bar people could not use:
 * a touchpad without scrolling, or somebody who expects to drag it. */
static int g_drag_bar = 0, g_drag_y0 = 0, g_drag_s0 = 0;

static int on_scroll_track(int x, int y)
{
    if (g_content_h <= g_view_h) return 0;
    int tx = g_card.right - S(13);
    return x >= tx - S(8) && x <= tx + S(14) && y >= g_body.top && y <= g_body.bottom;
}

static void scroll_by(int dy)
{
    int m = scroll_max();
    g_scroll[scr_i()] += dy;
    if (g_scroll[scr_i()] > m) g_scroll[scr_i()] = m;
    if (g_scroll[scr_i()] < 0) g_scroll[scr_i()] = 0;
}

static void focus_visible(void)
{
    if (g_focus < 0 || g_focus >= g_nw) return;
    int id = g_w[g_focus].id;
    if (id == ID_PRIMARY || id == ID_BACK || id == ID_QUIT || id == ID_LATER ||
        id == ID_HELPER || id == ID_STOP || id == ID_LEAVE || id == ID_REPORT ||
        id == ID_TERMS || id == ID_LICENSES) return;
    RECT r = g_w[g_focus].r;
    if (r.top < g_body.top)        scroll_by(r.top - g_body.top - S(12));
    else if (r.bottom > g_body.bottom) scroll_by(r.bottom - g_body.bottom + S(12));
}

static void focus_step(int dir)
{
    if (g_nw <= 0) return;
    int i = (g_focus < 0) ? (dir > 0 ? -1 : 0) : g_focus;
    for (int n = 0; n < g_nw; n++) {
        i = (i + dir + g_nw) % g_nw;
        if (g_w[i].enabled) { g_focus = i; break; }
    }
    g_focus_ring = 1;
    focus_visible();
}

/* ═══════════════════════════════════════════════════════════════════
 *  Device plumbing: DPI, fonts, back buffer
 * ═══════════════════════════════════════════════════════════════════ */
typedef BOOL (WINAPI *PFN_SPDAC)(HANDLE);
typedef BOOL (WINAPI *PFN_SPDA)(void);
typedef UINT (WINAPI *PFN_GDFW)(HWND);
typedef UINT (WINAPI *PFN_GDFS)(void);
typedef HRESULT (WINAPI *PFN_DWMSWA)(HWND, DWORD, LPCVOID, DWORD);

static void dpi_opt_in(void)
{
    /* Newest API first, then the Win7 one, both through GetProcAddress so
     * the binary still loads on a machine that has neither. */
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (u) {
        PFN_SPDAC f = (PFN_SPDAC)(void *)GetProcAddress(u, "SetProcessDpiAwarenessContext");
        if (f && f((HANDLE)(INT_PTR)-4)) return;   /* PER_MONITOR_AWARE_V2 */
        PFN_SPDA g = (PFN_SPDA)(void *)GetProcAddress(u, "SetProcessDPIAware");
        if (g) g();
    }
}

static int dpi_for(HWND h)
{
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (u) {
        PFN_GDFW f = (PFN_GDFW)(void *)GetProcAddress(u, "GetDpiForWindow");
        if (f && h) { UINT d = f(h); if (d >= 72) return (int)d; }
        PFN_GDFS g = (PFN_GDFS)(void *)GetProcAddress(u, "GetDpiForSystem");
        if (g) { UINT d = g(); if (d >= 72) return (int)d; }
    }
    HDC dc = GetDC(NULL);
    int d = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(NULL, dc);
    return d >= 72 ? d : 96;
}

/* THE UI FACE: Atkinson Hyperlegible Next, drawn for readers with low
 * vision (I, l and 1 differ), and the face the website is set in
 * (src/aurbridge/fonts/OFL.txt, no Reserved Font Name). Three static
 * faces travel inside this file (aurbridge-info.rc) and are lent to this
 * process only, so nothing is installed: 400 and 700 are one family at
 * FW_NORMAL and FW_BOLD, and 800 (the page title) is a family of its own
 * at FW_NORMAL, which is how GDI sees them (the legacy family name).
 * A build without them, or a Windows that will not take them, gets
 * Segoe UI (bold for the title), which every Windows has. */
#define FACE_TEXT     L"Atkinson Hyperlegible Next"
#define FACE_DISPLAY  L"Atkinson Hyperlegible Next ExtraBold"
#define FACE_FALLBACK L"Segoe UI"
static int g_face_ok;

static void fonts_load(void)
{
    static const int ids[] = { 100, 101, 102 };
    int ok = 0;
    for (int i = 0; i < 3; i++) {
        HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(ids[i]), L"AURFONT");
        HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
        void *b = g ? LockResource(g) : NULL;
        DWORD n = 0;
        if (b && AddFontMemResourceEx(b, SizeofResource(NULL, r), NULL, &n) && n) ok++;
    }
    g_face_ok = ok == 3;
    g_face_ok_early = g_face_ok;
}

static void fonts_free(void)
{
    for (int i = 0; i < N_TYPE; i++)
        if (*TYPE[i].f) { DeleteObject(*TYPE[i].f); *TYPE[i].f = NULL; }
}

/* SIZES ARE FOR HER EYES (docs/EASY.md, rule 3): nothing she reads is
 * under 18 px at 96 dpi. Hierarchy is size and weight in one face: a
 * bold title, regular text, bold where a line must be found first. */
static void fonts_make(void)
{
    fonts_free();
    HDC dc = g_mdc ? g_mdc : GetDC(NULL);
    if (dc) SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    for (int i = 0; i < N_TYPE; i++) {
        int px = TYPE[i].px;
        if (g_dense && px == 20 && TYPE[i].f != &g_f_btn) px = 18;
        if (g_dense && px == 32) px = 28;
        *TYPE[i].f = CreateFontW(-S(px), 0, 0, 0,
                                 g_face_ok || !TYPE[i].display ? TYPE[i].weight : FW_BOLD,
                                 0, 0, 0,
                                 DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, (BYTE)(VARIABLE_PITCH | FF_SWISS),
                                 !g_face_ok ? FACE_FALLBACK
                                            : TYPE[i].display ? FACE_DISPLAY : FACE_TEXT);
        TEXTMETRICW tm;
        int th = S(px);
        if (dc) {
            SelectObject(dc, *TYPE[i].f);
            if (GetTextMetricsW(dc, &tm)) th = tm.tmHeight;
        }
        int lh = (int)((float)S(px) * (g_dense && TYPE[i].lh > 1.4f ? 1.40f : TYPE[i].lh) + 0.5f);
        TYPE[i].tm = th;
        TYPE[i].line = lh > th ? lh : th;
    }
    if (dc) SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    if (dc && dc != g_mdc) ReleaseDC(NULL, dc);
}

static void backbuffer(int w, int h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (g_mdc && g_mw == w && g_mh == h) return;
    if (g_mdc) {
        SelectObject(g_mdc, g_moldbmp);
        DeleteObject(g_mbmp);
        DeleteDC(g_mdc);
        g_mdc = NULL; g_mbmp = NULL; g_px = NULL;
    }
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;          /* top-down */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC sdc = GetDC(NULL);
    g_mdc  = CreateCompatibleDC(sdc);
    g_mbmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, (void **)&g_px, NULL, 0);
    if (sdc) ReleaseDC(NULL, sdc);
    if (g_mdc && g_mbmp) {
        g_moldbmp = (HBITMAP)SelectObject(g_mdc, g_mbmp);
        SetBkMode(g_mdc, TRANSPARENT);
        SetStretchBltMode(g_mdc, HALFTONE);
    }
    g_mw = w; g_mh = h;
    clip_reset();
}

/* Dev harness: dump the back buffer. Used by --shot/--selftest only. */
static int save_bmp(const char *path)
{
    if (!g_px) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    uint32_t bytes = (uint32_t)g_mw * (uint32_t)g_mh * 4u;
    uint16_t tag = 0x4D42, zero = 0;
    uint32_t off = 14u + 40u, size = off + bytes;
    BITMAPINFOHEADER ih;
    memset(&ih, 0, sizeof ih);
    ih.biSize = sizeof ih; ih.biWidth = g_mw; ih.biHeight = -g_mh;
    ih.biPlanes = 1; ih.biBitCount = 32; ih.biCompression = BI_RGB;
    fwrite(&tag, 2, 1, f); fwrite(&size, 4, 1, f);
    fwrite(&zero, 2, 1, f); fwrite(&zero, 2, 1, f); fwrite(&off, 4, 1, f);
    fwrite(&ih, 40, 1, f);
    fwrite(g_px, bytes, 1, f);
    fclose(f);
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════
 *  For a screen reader
 *
 *  Everything here is drawn by hand, so to Narrator the window was one
 *  nameless rectangle: nothing to read, nothing to press. This answers
 *  WM_GETOBJECT with a small MSAA object instead. The page has a name
 *  (its heading, and where it stands), and every button and look card
 *  on it is a child with its own name, role, state, place and default
 *  action -- the same widget list the mouse and keyboard use, so what
 *  is announced is what is there. When the page changes, the object
 *  says so, and points at the button Enter would press.
 * ═══════════════════════════════════════════════════════════════════ */
static const IID ACC_IID_IUnknown   = { 0x00000000, 0x0000, 0x0000,
                                       { 0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46 } };
static const IID ACC_IID_IDispatch  = { 0x00020400, 0x0000, 0x0000,
                                       { 0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46 } };
static const IID ACC_IID_IAccessible = { 0x618736E0, 0x3C3D, 0x11CF,
                                       { 0x81,0x0C,0x00,0xAA,0x00,0x38,0x9B,0x71 } };

#define WM_ACC_ACTIVATE (WM_APP + 7)

static IAccessible *g_acc_std;         /* Windows' own, for parent and siblings */
static wchar_t      g_acc_name[600];   /* the page's name, as last announced    */
static int          g_acc_page = -1;

/* The page, in a sentence or two: what she would read first. Stable
 * while the page stands still -- no seconds, no minutes -- so that a
 * screen reader is told when something changed, not every second. */
static void acc_page_name(wchar_t *out, int n)
{
    out[0] = 0;
    int k = 0;
#define SAY(...) do { if (k >= 0 && k < n - 1) { int w_ = _snwprintf(out + k, (size_t)(n - k), __VA_ARGS__); k = w_ < 0 ? -1 : k + w_; } } while (0)
    switch (g_pane ? -1 : (int)g_page) {
    case -1:
        SAY(L"%ls. You can read this without the internet. Use the arrow keys to scroll. "
            L"Press Enter or Escape when you are done reading.", pane_title());
        break;
    case PAGE_WELCOME:
        SAY(L"Let’s put AurOS on this computer. %ls",
            welcome_passed() ? (g_dl_state == 1
                ? L"This computer can take AurOS. AurOS is downloading while you read: about "
                  L"2 GB, and about 5 GB more once it is unpacked. If your internet has a data "
                  L"limit, close this window: closing deletes it. Press Enter for Next."
                : L"This computer can take AurOS. Press Enter for Next.")
            : (g_pf_valid && !g_checking && !pf_is_go(&g_report))
                ? L"Something needs sorting out first."
                : L"Looking at this computer. This only looks; nothing is changed.");
        break;
    case PAGE_BLOCKED:
        if (sb_page()) {
            SAY(L"One setting needs changing first. This computer is set to start only "
                L"Windows. Someone who knows computers can change it in about five "
                L"minutes. Nothing on this computer has been changed. Press Enter for "
                L"%ls.", g_sb_helper ? L"Check again" : L"Show a helper how");
            break;
        }
        SAY(L"%ls. Nothing on this computer has been changed. ",
            any_cannot() ? L"AurOS can’t go on this computer yet"
                         : L"A few things to sort out first");
        /* each problem, and what happens about it: not just its name */
        for (int i = 0; i < g_report.n && k > 0 && k < n - 200; i++) {
            const pf_result *r = &g_report.results[i];
            if (r->sev != PF_BLOCK) continue;
            if (any_cannot() && kind_of(r) != FIX_CANNOT) continue;   /* as on the screen */
            wchar_t prob[160], fix[600];
            stop_line(r, any_cannot(), prob, fix);
            SAY(L"%ls. %ls ", prob, fix);
        }
        break;
    case PAGE_STYLE:
        SAY(L"Pick the look you want. Four looks, each a picture of the screen. "
            L"Chosen: %ls.", STYLES[style_ok() ? g_sel_style : 0].name);
        break;
    case PAGE_READY: {
        int lo, hi;
        eta_range(&lo, &hi);
        wchar_t rng[64];
        range_words(lo, hi, rng, 64);
        SAY(L"Ready to install. Windows and your files are kept. Setup makes Windows smaller to "
            L"fit AurOS. If the power fails or the computer crashes during that step, Windows "
            L"or your files could be damaged and might not come back. Any change to a drive "
            L"carries some risk. Copy anything you can’t replace to a memory stick first. "
            L"%ls The computer restarts once. It takes about %ls. By pressing Install AurOS "
            L"you agree to the AurOS Terms; the button Read the Terms shows them. Press "
            L"Enter to install AurOS.",
            STYLES[style_ok() ? g_sel_style : 0].ready, rng);
        break;
    }
    case PAGE_PROGRESS:
        if (install_ok())
            SAY(L"Ready to restart. Save anything you’re working on, then press "
                L"Enter to restart now. Or press Restart later: nothing restarts until "
                L"you come back and press Restart now.");
        else if (install_failed())
            SAY(L"The install stopped. Setup stopped. If Windows does not start, ask "
                L"someone to help you. Press Enter to try again.");
        else if (g_stop_wanted)
            SAY(L"Stopping. When the step it is on is done, everything is put back and "
                L"this window closes by itself.");
        else
            SAY(L"Setting up AurOS. You can walk away. When you come back, press "
                L"Restart now.");
        break;
    default: break;
    }
#undef SAY
    out[n - 1] = 0;
}

/* What a screen reader says under the name, when it is asked: nothing the
 * page does not already say on its screen. */
static const wchar_t *acc_description(int id)
{
    /* the big button, pressed too early, says why it waits */
    if (id == ID_PRIMARY && primary_visible() && !primary_enabled()) return early_words();
    return NULL;
}

static int acc_primary_index(void)
{
    for (int i = 0; i < g_nw; i++) if (g_w[i].id == ID_PRIMARY) return i;
    return -1;
}

/* -1 the page itself, 0.. a widget, -2 not one of ours */
static int acc_child(VARIANT v)
{
    if (v.vt != VT_I4) return -2;
    if (v.lVal == CHILDID_SELF) return -1;
    if (v.lVal >= 1 && v.lVal <= g_nw) return (int)v.lVal - 1;
    return -2;
}

static BSTR acc_str(const wchar_t *s) { return s ? SysAllocString(s) : NULL; }

static HRESULT STDMETHODCALLTYPE acc_QueryInterface(IAccessible *t, REFIID riid, void **pv)
{
    if (IsEqualIID(riid, &ACC_IID_IUnknown) || IsEqualIID(riid, &ACC_IID_IDispatch) ||
        IsEqualIID(riid, &ACC_IID_IAccessible)) {
        *pv = t;
        return S_OK;
    }
    *pv = NULL;
    return E_NOINTERFACE;
}
/* One object for the life of the window: nothing to count, nothing to free. */
static ULONG STDMETHODCALLTYPE acc_AddRef(IAccessible *t)  { (void)t; return 2; }
static ULONG STDMETHODCALLTYPE acc_Release(IAccessible *t) { (void)t; return 1; }
static HRESULT STDMETHODCALLTYPE acc_GetTypeInfoCount(IAccessible *t, UINT *n)
{ (void)t; *n = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE acc_GetTypeInfo(IAccessible *t, UINT i, LCID l, ITypeInfo **p)
{ (void)t; (void)i; (void)l; *p = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE acc_GetIDsOfNames(IAccessible *t, REFIID r, LPOLESTR *s,
                                                   UINT c, LCID l, DISPID *d)
{ (void)t; (void)r; (void)s; (void)c; (void)l; (void)d; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE acc_Invoke(IAccessible *t, DISPID d, REFIID r, LCID l, WORD f,
                                            DISPPARAMS *p, VARIANT *v, EXCEPINFO *e, UINT *a)
{ (void)t; (void)d; (void)r; (void)l; (void)f; (void)p; (void)v; (void)e; (void)a;
  return E_NOTIMPL; }

static HRESULT STDMETHODCALLTYPE acc_get_accParent(IAccessible *t, IDispatch **p)
{
    (void)t;
    if (g_acc_std) return g_acc_std->lpVtbl->get_accParent(g_acc_std, p);
    *p = NULL;
    return S_FALSE;
}
static HRESULT STDMETHODCALLTYPE acc_get_accChildCount(IAccessible *t, LONG *n)
{ (void)t; *n = g_nw; return S_OK; }
static HRESULT STDMETHODCALLTYPE acc_get_accChild(IAccessible *t, VARIANT v, IDispatch **p)
{ (void)t; *p = NULL; return acc_child(v) == -2 ? E_INVALIDARG : S_FALSE; }

static HRESULT STDMETHODCALLTYPE acc_get_accName(IAccessible *t, VARIANT v, BSTR *s)
{
    (void)t;
    int c = acc_child(v);
    if (c == -2) return E_INVALIDARG;
    if (c == -1) { wchar_t b[600]; acc_page_name(b, 600); *s = acc_str(b); return S_OK; }
    *s = acc_str(g_w[c].name);
    return *s ? S_OK : S_FALSE;
}
static HRESULT STDMETHODCALLTYPE acc_get_accValue(IAccessible *t, VARIANT v, BSTR *s)
{ (void)t; (void)v; *s = NULL; return S_FALSE; }
static HRESULT STDMETHODCALLTYPE acc_get_accDescription(IAccessible *t, VARIANT v, BSTR *s)
{
    (void)t;
    int c = acc_child(v);
    if (c == -2) return E_INVALIDARG;
    if (c >= 0 && g_w[c].id >= ID_STYLE && g_w[c].id < ID_STYLE + N_STYLES) {
        *s = acc_str(STYLES[g_w[c].id - ID_STYLE].line);
        return S_OK;
    }
    *s = c >= 0 ? acc_str(acc_description(g_w[c].id)) : NULL;
    return *s ? S_OK : S_FALSE;
}
static HRESULT STDMETHODCALLTYPE acc_get_accRole(IAccessible *t, VARIANT v, VARIANT *r)
{
    (void)t;
    int c = acc_child(v);
    if (c == -2) return E_INVALIDARG;
    r->vt = VT_I4;
    r->lVal = c == -1 ? ROLE_SYSTEM_CLIENT
            : g_w[c].kind == W_CARD ? ROLE_SYSTEM_RADIOBUTTON
            : g_w[c].kind == W_TOGGLE ? ROLE_SYSTEM_CHECKBUTTON : ROLE_SYSTEM_PUSHBUTTON;
    return S_OK;
}
static int acc_is_focused(int c)
{
    if (GetFocus() != g_hwnd) return 0;
    if (g_focus >= 0) return c == g_focus;
    return c == acc_primary_index();     /* what Enter presses */
}
static HRESULT STDMETHODCALLTYPE acc_get_accState(IAccessible *t, VARIANT v, VARIANT *r)
{
    (void)t;
    int c = acc_child(v);
    if (c == -2) return E_INVALIDARG;
    r->vt = VT_I4;
    if (c == -1) {
        r->lVal = STATE_SYSTEM_FOCUSABLE | (GetFocus() == g_hwnd ? STATE_SYSTEM_FOCUSED : 0);
        return S_OK;
    }
    LONG st = STATE_SYSTEM_FOCUSABLE;
    if (!g_w[c].enabled) st |= STATE_SYSTEM_UNAVAILABLE;
    if (acc_is_focused(c)) st |= STATE_SYSTEM_FOCUSED;
    if (g_w[c].kind == W_CARD && g_w[c].id - ID_STYLE == g_sel_style)
        st |= STATE_SYSTEM_CHECKED | STATE_SYSTEM_SELECTED;
    if (g_w[c].id == ID_WIFI && g_bring_wifi) st |= STATE_SYSTEM_CHECKED;
    r->lVal = st;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE acc_get_accHelp(IAccessible *t, VARIANT v, BSTR *s)
{ (void)t; (void)v; *s = NULL; return S_FALSE; }
static HRESULT STDMETHODCALLTYPE acc_get_accHelpTopic(IAccessible *t, BSTR *f, VARIANT v, LONG *id)
{ (void)t; (void)v; *f = NULL; *id = 0; return S_FALSE; }
static HRESULT STDMETHODCALLTYPE acc_get_accKeyboardShortcut(IAccessible *t, VARIANT v, BSTR *s)
{
    (void)t;
    int c = acc_child(v);
    if (c == -2) return E_INVALIDARG;
    *s = (c >= 0 && g_w[c].id == ID_PRIMARY) ? acc_str(L"Enter")
       : (c >= 0 && g_w[c].id == ID_BACK) ? acc_str(L"Escape")
       : NULL;
    return *s ? S_OK : S_FALSE;
}
static HRESULT STDMETHODCALLTYPE acc_get_accFocus(IAccessible *t, VARIANT *r)
{
    (void)t;
    if (GetFocus() != g_hwnd) { r->vt = VT_EMPTY; return S_FALSE; }
    int c = g_focus >= 0 ? g_focus : acc_primary_index();
    r->vt = VT_I4;
    r->lVal = c >= 0 ? c + 1 : CHILDID_SELF;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE acc_get_accSelection(IAccessible *t, VARIANT *r)
{
    (void)t;
    for (int i = 0; i < g_nw; i++)
        if (g_w[i].kind == W_CARD && g_w[i].id - ID_STYLE == g_sel_style) {
            r->vt = VT_I4; r->lVal = i + 1; return S_OK;
        }
    r->vt = VT_EMPTY;
    return S_FALSE;
}
static HRESULT STDMETHODCALLTYPE acc_get_accDefaultAction(IAccessible *t, VARIANT v, BSTR *s)
{
    (void)t;
    int c = acc_child(v);
    if (c == -2) return E_INVALIDARG;
    *s = c < 0 ? NULL : acc_str(g_w[c].kind == W_CARD ? L"Choose"
                              : g_w[c].kind == W_TOGGLE ? (g_bring_wifi ? L"Untick" : L"Tick")
                              : L"Press");
    return *s ? S_OK : S_FALSE;
}
static HRESULT STDMETHODCALLTYPE acc_accSelect(IAccessible *t, LONG flags, VARIANT v)
{
    (void)t; (void)flags;
    int c = acc_child(v);
    if (c < 0) return E_INVALIDARG;
    PostMessageW(g_hwnd, WM_ACC_ACTIVATE, (WPARAM)g_w[c].id, 0);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE acc_accLocation(IAccessible *t, LONG *x, LONG *y, LONG *w,
                                                 LONG *h, VARIANT v)
{
    (void)t;
    int c = acc_child(v);
    if (c == -2) return E_INVALIDARG;
    RECT r;
    if (c == -1) GetClientRect(g_hwnd, &r); else r = g_w[c].r;
    POINT p = { r.left, r.top };
    ClientToScreen(g_hwnd, &p);
    *x = p.x; *y = p.y; *w = r.right - r.left; *h = r.bottom - r.top;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE acc_accNavigate(IAccessible *t, LONG dir, VARIANT from, VARIANT *r)
{
    (void)t;
    int c = acc_child(from);
    r->vt = VT_EMPTY;
    if (c == -2) return E_INVALIDARG;
    int to = -2;
    if (c == -1) {
        if (dir == NAVDIR_FIRSTCHILD) to = 0;
        else if (dir == NAVDIR_LASTCHILD) to = g_nw - 1;
        else if (g_acc_std) return g_acc_std->lpVtbl->accNavigate(g_acc_std, dir, from, r);
    } else {
        if (dir == NAVDIR_NEXT || dir == NAVDIR_DOWN || dir == NAVDIR_RIGHT) to = c + 1;
        if (dir == NAVDIR_PREVIOUS || dir == NAVDIR_UP || dir == NAVDIR_LEFT) to = c - 1;
    }
    if (to < 0 || to >= g_nw) return S_FALSE;
    r->vt = VT_I4; r->lVal = to + 1;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE acc_accHitTest(IAccessible *t, LONG x, LONG y, VARIANT *r)
{
    (void)t;
    POINT p = { x, y };
    ScreenToClient(g_hwnd, &p);
    RECT cr;
    GetClientRect(g_hwnd, &cr);
    if (!PtInRect(&cr, p)) { r->vt = VT_EMPTY; return S_FALSE; }
    int i = w_find_at(p.x, p.y);
    r->vt = VT_I4;
    r->lVal = i >= 0 ? i + 1 : CHILDID_SELF;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE acc_accDoDefaultAction(IAccessible *t, VARIANT v)
{
    (void)t;
    int c = acc_child(v);
    if (c < 0) return c == -1 ? DISP_E_MEMBERNOTFOUND : E_INVALIDARG;
    if (!g_w[c].enabled) return E_FAIL;
    /* On the window's own thread, between messages, as a click is. */
    PostMessageW(g_hwnd, WM_ACC_ACTIVATE, (WPARAM)g_w[c].id, 0);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE acc_put_accName(IAccessible *t, VARIANT v, BSTR s)
{ (void)t; (void)v; (void)s; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE acc_put_accValue(IAccessible *t, VARIANT v, BSTR s)
{ (void)t; (void)v; (void)s; return E_NOTIMPL; }

static IAccessibleVtbl ACC_VTBL = {
    acc_QueryInterface, acc_AddRef, acc_Release,
    acc_GetTypeInfoCount, acc_GetTypeInfo, acc_GetIDsOfNames, acc_Invoke,
    acc_get_accParent, acc_get_accChildCount, acc_get_accChild, acc_get_accName,
    acc_get_accValue, acc_get_accDescription, acc_get_accRole, acc_get_accState,
    acc_get_accHelp, acc_get_accHelpTopic, acc_get_accKeyboardShortcut,
    acc_get_accFocus, acc_get_accSelection, acc_get_accDefaultAction,
    acc_accSelect, acc_accLocation, acc_accNavigate, acc_accHitTest,
    acc_accDoDefaultAction, acc_put_accName, acc_put_accValue,
};
static IAccessible g_acc = { &ACC_VTBL };

/* After each paint: when the page (or where it stands) changed, say so,
 * and point at the button Enter would press. */
static void acc_after_render(void)
{
    if (!g_hwnd || g_shot_mode || g_nav_mode) return;
    wchar_t b[600];
    acc_page_name(b, 600);
    if (wcscmp(b, g_acc_name) != 0) {
        wcscpy(g_acc_name, b);
        NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, g_hwnd, OBJID_CLIENT, CHILDID_SELF);
    }
    if (g_acc_page != (int)g_page) {
        g_acc_page = (int)g_page;
        int p = acc_primary_index();
        if (p >= 0 && GetFocus() == g_hwnd)
            NotifyWinEvent(EVENT_OBJECT_FOCUS, g_hwnd, OBJID_CLIENT, p + 1);
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  Window
 * ═══════════════════════════════════════════════════════════════════ */
static int g_paints;
static char g_selftest_dir[MAX_PATH];

/* The stop page's quiet re-check came back clean. It goes back to the
 * first screen, which then says so -- NEVER straight into the install,
 * even when the stop came from the check behind "Install AurOS": she
 * presses that button again, with her look still chosen. */
static void blocked_cleared(void)
{
    g_check_next = PAGE_STYLE;
    check_advance();
}

static void selftest_log(void)
{
    char path[MAX_PATH + 32];
    _snprintf(path, sizeof path - 1, "%s/selftest-window.bmp", g_selftest_dir);
    path[sizeof path - 1] = 0;
    save_bmp(path);
    _snprintf(path, sizeof path - 1, "%s/selftest.log", g_selftest_dir);
    path[sizeof path - 1] = 0;
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "window created   : %s\n", g_hwnd ? "yes" : "no");
    fprintf(f, "client size      : %dx%d @ %d dpi\n", g_cw, g_ch, g_dpi);
    fprintf(f, "WM_PAINT handled : %d\n", g_paints);
    fprintf(f, "back buffer      : %s\n", g_px ? "ok" : "MISSING");
    fprintf(f, "page             : %d\n", (int)g_page);
    fprintf(f, "preflight done   : %d (blocks=%d warns=%d results=%d)\n",
            g_pf_valid, g_report.n_block, g_report.n_warn, g_report.n);
    fprintf(f, "widgets on page  : %d\n", g_nw);
    fprintf(f, "focus after Tab  : %d (ring=%d, id=%d)\n", g_focus, g_focus_ring,
            (g_focus >= 0 && g_focus < g_nw) ? g_w[g_focus].id : -1);
    fprintf(f, "furthest page    : %d (%s)\n", g_max_page,
            g_max_page >= (int)PAGE_STYLE ? "PAST THE GATE" : "gate held");
    /* WHAT A SCREEN READER GETS, asked the way one asks: through
     * oleacc, from the window, not by calling our own object. */
    {
        IAccessible *a = NULL;
        char nm[400] = "", bt[120] = "";
        long kids = -1;
        if (AccessibleObjectFromWindow(g_hwnd, (DWORD)OBJID_CLIENT, &ACC_IID_IAccessible,
                                       (void **)&a) == S_OK && a) {
            VARIANT self; self.vt = VT_I4; self.lVal = CHILDID_SELF;
            BSTR s = NULL;
            if (a->lpVtbl->get_accName(a, self, &s) == S_OK && s)
                WideCharToMultiByte(CP_UTF8, 0, s, -1, nm, sizeof nm, NULL, NULL);
            SysFreeString(s);
            LONG n = 0;
            if (a->lpVtbl->get_accChildCount(a, &n) == S_OK) kids = n;
            int pi = acc_primary_index();
            if (pi >= 0) {
                VARIANT ch; ch.vt = VT_I4; ch.lVal = pi + 1;
                s = NULL;
                if (a->lpVtbl->get_accName(a, ch, &s) == S_OK && s)
                    WideCharToMultiByte(CP_UTF8, 0, s, -1, bt, sizeof bt, NULL, NULL);
                SysFreeString(s);
            }
            a->lpVtbl->Release(a);
        }
        fprintf(f, "screen reader    : \"%s\", %ld parts, button \"%s\"\n", nm, kids, bt);
    }
    fprintf(f, "nav_allowed style..progress: ");
    for (int pg = (int)PAGE_STYLE; pg < (int)PAGE_COUNT; pg++)
        fprintf(f, "%d", nav_allowed((page_id)pg));
    fprintf(f, "\n");
    fclose(f);
}

static void bring_forward(void)
{
    if (!g_hwnd || g_nav_mode || g_shot_mode) return;
    if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
    FlashWindow(g_hwnd, TRUE);
}

/* The window's X, and the Stop and Close buttons. 1: it may close now. 0:
 * not yet -- the question is on the screen (g_confirm), or it is already
 * stopping. Closing with nothing to lose, or nothing armed, is at once. */
static int close_asked(HWND h)
{
    (void)h;
    if (g_confirm || g_stop_wanted) return 0;
    if (g_install_running) { g_confirm = 1; page_changed(); return 0; }
    /* READY, AND ABOUT TO BE THROWN AWAY. After the last step the
     * window's X used to close it and take back the restart without
     * a word. */
    if (g_install_armed && !g_restarting) { g_confirm = 2; page_changed(); return 0; }
    return 1;
}

/* She answered "undo" to the question. 1: the window can go now. 0: a step
 * is mid-way, it finishes first and then everything is taken back
 * (stop_tick), and the page says "Stopping." */
static int leave_confirmed(void)
{
    int what = g_confirm;
    g_confirm = 0;
    page_changed();
    if (what == 3) return 0;               /* "Not yet": back to the page */
    if (what != 1) return 1;
    /* Before the engine has started (it waits for the download that ran
     * behind the pages, which only ever writes \AurOS\ ), there is
     * nothing to finish: it never starts, and the window goes. */
    if (InterlockedCompareExchange(&g_ab_gate, 2, 0) != 1) return 1;
    if (g_ab_state == 1) { InterlockedExchange(&g_stop_wanted, 1); return 0; }
    return 1;
}

/* 1 when she asked to stop and the step has now ended: everything has
 * been taken back, and the window can go. */
static int stop_tick(void)
{
    if (!g_stop_wanted || g_ab_state == 1) return 0;
    install_cancel();
    g_install_running = 0;
    return 1;
}

static void tick(void)
{
    g_ticks++;
    int need = 0;
    dl_measure(GetTickCount64());

    if (pf_poll()) {
        g_settle = 0;
        if (g_page == PAGE_WELCOME && g_check_next != PAGE_PROGRESS) {
            /* the answer shows for a moment first (the settle, below) */
        } else if (g_page == PAGE_BLOCKED && g_report.n_block == 0) {
            blocked_cleared();
        } else {
            check_advance();
        }
        need = 1;
    }
    if (g_checking) need = 1;

    if (g_page == PAGE_WELCOME && g_pf_valid && !g_checking) {
        /* The answer stands for half a second before Next takes the
         * keyboard, so it is seen as an answer and not a flicker. */
        if (g_settle >= 0 && ++g_settle == 8) { check_advance(); need = 1; }
    } else if (g_page == PAGE_PROGRESS) {
        install_tick();
        if (stop_tick()) { DestroyWindow(g_hwnd); return; }
        if (g_install_running) {
            int ph, pc;
            read_engine(&ph, &pc);
            if (ph < 0) ph = 0;
            if (early_download_running() && ph < AB_PREPARE) ph = AB_PREPARE;
            float f = overall_fraction(ph, pc);
            if (f > g_bar_f) g_bar_f = f;
            eta_sample(g_bar_f, GetTickCount64());
        }
        /* Ready: it says so, and comes to the front, and waits for her. */
        static int told;
        if (install_ok() && !told && !g_selftest) {
            told = 1;
            bring_forward();
        }
        need = 1;
    } else if (g_page == PAGE_BLOCKED && !g_selftest) {
        if (bg_recheck_tick() && g_report.n_block == 0) blocked_cleared();
        need = 1;
    }

    /* --selftest: wait for the check, then press Enter (Next) and Tab,
     * the way a person with a keyboard would. */
    if (g_selftest) {
        static int step = 0, at = 0;
        int settled = g_pf_valid && !g_checking &&
                      (g_page == PAGE_BLOCKED ||
                       (g_page == PAGE_WELCOME && g_settle > 8));
        /* and the page has stood still long enough for a press to count */
        if (step == 0 && settled && now_ms() - g_changed_at > PRESS_SETTLE_MS + 100) {
            step = 1; at = g_ticks;
            /* NEVER ON THE STOP PAGE: Enter there is "Fix these for me",
             * which can empty folders or restart the machine running the
             * test. Only a clean first page gets its Next pressed. */
            if (g_page == PAGE_WELCOME)
                PostMessageW(g_hwnd, WM_KEYDOWN, VK_RETURN, 0);
        } else if (step == 1 && g_ticks > at + 6) {
            step = 2; at = g_ticks;
            PostMessageW(g_hwnd, WM_KEYDOWN, VK_TAB, 0);
        } else if ((step == 2 && g_ticks > at + 6) || g_ticks > 9000) {
            selftest_log();
            PostQuitMessage(0);
            return;
        }
    }
    if (need && g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

/* Every key, in one place, so --navtest presses the same ones. */
static void on_key(WPARAM k)
{
    int fk = (g_focus >= 0 && g_focus < g_nw) ? (int)g_w[g_focus].kind : -1;
    switch (k) {
    case VK_TAB:
        focus_step((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
        break;
    /* ENTER AND SPACE PRESS THE BIG BUTTON, unless she has tabbed to
     * another one. On a picture card they choose it, and go on. */
    case VK_RETURN:
    case VK_SPACE: {
        int id = (fk >= 0 && !(fk == W_TOGGLE && k == VK_RETURN)) ? g_w[g_focus].id : ID_PRIMARY;
        if (!press_allowed(id)) break;
        if (fk == W_TOGGLE) {
            if (k == VK_SPACE) widget_do(g_w[g_focus].id);
            else do_primary();
        }
        else if (fk == W_BUTTON) widget_do(g_w[g_focus].id);
        else if (fk == W_CARD) { widget_do(g_w[g_focus].id); do_primary(); }
        else do_primary();
        break;
    }
    /* ESC NEVER DESTROYS ANYTHING: it stops the restart count, or goes
     * back a page, and that is all. It used to close the window. */
    case VK_ESCAPE:
        if (g_confirm) { g_confirm = 0; page_changed(); }
        else if (g_pane) pane_close();
        else if (back_visible()) goto_page(back_target());
        break;
    case VK_LEFT:
    case VK_RIGHT:
        if (g_page == PAGE_STYLE) {
            select_style(g_sel_style + (k == VK_RIGHT ? 1 : -1));
            g_want_focus_id = ID_STYLE + g_sel_style;
            g_focus_ring = 1;
        } else focus_step(k == VK_RIGHT ? 1 : -1);
        break;
    case VK_UP:    scroll_by(-S(60)); break;
    case VK_DOWN:  scroll_by(S(60));  break;
    case VK_PRIOR: scroll_by(-g_view_h + S(40)); break;
    case VK_NEXT:  scroll_by(g_view_h - S(40));  break;
    case VK_HOME:  g_scroll[scr_i()] = 0; break;
    case VK_END:   g_scroll[scr_i()] = scroll_max(); break;
    default: break;
    }
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE:
        g_hwnd = h;
        g_dpi  = dpi_for(h);
        fonts_make();
        /* The self-test drives its own clock so a slow emulated desktop
         * cannot stretch a short script into minutes. */
        if (!g_selftest) SetTimer(h, 1, 60, NULL);
        return 0;

    case WM_SIZE:
        g_cw = LOWORD(lp); g_ch = HIWORD(lp);
        backbuffer(g_cw, g_ch);
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = S(900);
        mm->ptMinTrackSize.y = S(600);
        /* Never bigger than the screen: a minimum taller than a small
         * laptop's screen put the bottom of every page, and the buttons,
         * where nobody could reach them. */
        RECT wa;
        if (work_area(h, &wa)) {
            if (mm->ptMinTrackSize.x > wa.right - wa.left) mm->ptMinTrackSize.x = wa.right - wa.left;
            if (mm->ptMinTrackSize.y > wa.bottom - wa.top) mm->ptMinTrackSize.y = wa.bottom - wa.top;
        }
        return 0;
    }

    case WM_DPICHANGED: {
        g_dpi = (int)HIWORD(wp);
        fonts_make();
        RECT *r = (RECT *)lp;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left,
                     r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        backbuffer(g_cw, g_ch);
        render();
        g_paints++;
        if (g_px) BitBlt(dc, 0, 0, g_cw, g_ch, g_mdc, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_TIMER:
        tick();
        return 0;

    case WM_GETOBJECT:
        if ((LONG)lp == (LONG)OBJID_CLIENT) {
            if (!g_acc_std)
                CreateStdAccessibleObject(h, OBJID_CLIENT, &ACC_IID_IAccessible,
                                          (void **)&g_acc_std);
            return LresultFromObject(&ACC_IID_IAccessible, wp, (IUnknown *)&g_acc);
        }
        break;

    case WM_ACC_ACTIVATE:
        /* a screen reader pressed a button or chose a card */
        for (int i = 0; i < g_nw; i++)
            if (g_w[i].id == (int)wp && g_w[i].enabled) { widget_activate((int)wp); break; }
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_MOUSEMOVE: {
        g_mouse.x = GET_X_LPARAM(lp); g_mouse.y = GET_Y_LPARAM(lp);
        if (g_drag_bar) {
            int room = g_view_h > 0 ? g_view_h : 1;
            int mx = scroll_max();
            g_scroll[scr_i()] = g_drag_s0 + (int)((long long)(g_mouse.y - g_drag_y0)
                                                 * g_content_h / room);
            if (g_scroll[scr_i()] > mx) g_scroll[scr_i()] = mx;
            if (g_scroll[scr_i()] < 0) g_scroll[scr_i()] = 0;
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        int hot = w_find_at(g_mouse.x, g_mouse.y);
        if (hot != g_hot_idx) { g_hot_idx = hot; InvalidateRect(h, NULL, FALSE); }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_mouse.x = g_mouse.y = -1; g_hot_idx = -1;
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        g_mouse.x = GET_X_LPARAM(lp); g_mouse.y = GET_Y_LPARAM(lp);
        if (on_scroll_track(g_mouse.x, g_mouse.y)) {
            /* a press on the track jumps there, then drags from there */
            int mx = scroll_max();
            int span = g_body.bottom - g_body.top;
            if (span > 0) g_scroll[scr_i()] = (int)((long long)(g_mouse.y - g_body.top) * mx / span);
            if (g_scroll[scr_i()] > mx) g_scroll[scr_i()] = mx;
            if (g_scroll[scr_i()] < 0) g_scroll[scr_i()] = 0;
            g_drag_bar = 1; g_drag_y0 = g_mouse.y; g_drag_s0 = g_scroll[scr_i()];
            SetCapture(h);
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        g_mouse_down = 1;
        g_press_idx  = w_find_at(g_mouse.x, g_mouse.y);
        if (g_press_idx >= 0) { g_focus = g_press_idx; g_focus_ring = 0; }
        SetCapture(h);
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_LBUTTONUP: {
        g_mouse.x = GET_X_LPARAM(lp); g_mouse.y = GET_Y_LPARAM(lp);
        if (g_drag_bar) {
            g_drag_bar = 0;
            ReleaseCapture();
            return 0;
        }
        g_mouse_down = 0;
        ReleaseCapture();
        int up = w_find_at(g_mouse.x, g_mouse.y);
        if (up >= 0 && up == g_press_idx) widget_activate(g_w[up].id);
        g_press_idx = -1;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL:
        scroll_by(-GET_WHEEL_DELTA_WPARAM(wp) * S(60) / WHEEL_DELTA);
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(NULL, (LPCWSTR)(g_hot_idx >= 0 ? IDC_HAND : IDC_ARROW)));
            return TRUE;
        }
        break;

    case WM_KEYDOWN:
        /* a held-down Enter repeats; that is not another press */
        if ((wp == VK_RETURN || wp == VK_SPACE) && (lp & (1L << 30))) return 0;
        on_key(wp);
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_CLOSE:
        if (close_asked(h)) DestroyWindow(h);
        else InvalidateRect(h, NULL, FALSE);
        return 0;

    /* WINDOWS IS ENDING THE SESSION, from the Start menu or anything else:
     * nothing is armed (only Restart now arms), so that restart starts
     * Windows as usual. g_restarting is set by our own Restart now and by
     * the restart a fix asks for, and by nothing here. */
    case WM_ENDSESSION:
        return 0;

    case WM_DESTROY:
        KillTimer(h, 1);
        /* Take back everything the install did, if it was done. Not when
         * the window is going BECAUSE of the restart she pressed for. */
        if (!g_restarting) {
            install_cancel();
            install_forget();
        } else {
            plat_let_sleep();
        }
        PostQuitMessage(0);
        return 0;

    /* BACK FROM SLEEP, OR UNLOCKED: the window comes to the front, so the
     * page that waits for her is the first thing she sees (B-4). */
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) bring_forward();
        return TRUE;
    case WM_WTSSESSION_CHANGE:
        if (wp == WTS_SESSION_UNLOCK) bring_forward();
        return 0;

    default: break;
    }
    return DefWindowProcW(h, m, wp, lp);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Dev harness — fixtures, screenshots and the navigation test.
 *
 *  Rendering only: no window is shown, no input is accepted, no phase
 *  is started, and nav_allowed() is untouched. The reports below are
 *  test fixtures and exist only here.
 * ═══════════════════════════════════════════════════════════════════ */
static void fx_add(const char *id, pf_severity sev, const char *title)
{
    if (g_report.n >= PF_MAX_RESULTS) return;
    pf_result *x = &g_report.results[g_report.n++];
    memset(x, 0, sizeof *x);
    snprintf(x->id, sizeof x->id, "%s", id);
    snprintf(x->title, sizeof x->title, "%s", title);
    snprintf(x->remedy, sizeof x->remedy, "a synthetic remedy");
    x->sev = sev;
    if (sev == PF_BLOCK) g_report.n_block++;
    else if (sev == PF_WARN) g_report.n_warn++;
}

/* A 2017 laptop: one 256 GB drive, SSD or hard disk. */
static void fx_machine(int hdd)
{
    memset(&g_hw, 0, sizeof g_hw);
    memset(&g_report, 0, sizeof g_report);
    g_report.system_disk = 0;
    g_report.n_disks = 1;
    g_report.disks[0].index = 0;
    g_report.disks[0].is_system = 1;
    g_report.disks[0].size_bytes = 256060514304ULL;
    g_report.disks[0].seek_penalty = hdd;
    snprintf(g_report.disks[0].model, sizeof g_report.disks[0].model,
             "SAMSUNG MZ7LN256HAJQ-000H1");
    /* C:, 237 GiB, 90 GiB of it in use */
    g_report.n_volumes = 1;
    g_report.system_volume = 0;
    snprintf(g_report.volumes[0].mount, sizeof g_report.volumes[0].mount, "C:");
    snprintf(g_report.volumes[0].fs, sizeof g_report.volumes[0].fs, "NTFS");
    g_report.volumes[0].size_bytes = 237ULL * GIB;
    g_report.volumes[0].used_bytes = 90ULL * GIB;
    g_report.volumes[0].offline_shrinkable = 147ULL * GIB;
    g_report.volumes[0].shrink_measured = 1;
    fx_add("firmware-uefi", PF_PASS, "UEFI firmware");
    fx_add("space-measured", PF_INFO, "Free space measured");
    g_pf_valid = 1;
    g_checking = 0;
}

static void fx_ready(void) { fx_add("ready", PF_PASS, "This computer is ready"); }

/* A computer's hardware, judged by the real matcher (stream HW), and its
 * stop put into the check's report as the worker does:
 *   FXH_BCM     a Broadcom BCM4352 Wi-Fi card, no cable socket (stops her)
 *   FXH_BCM_ETH the same card with a network cable socket (a note)
 *   FXH_AX      an Intel AX201: fine
 *   FXH_CAMERA  the AX201 and a laptop camera that will not work yet */
enum { FXH_BCM = 1, FXH_BCM_ETH, FXH_AX, FXH_CAMERA };
static void fx_hw(int what)
{
    hw_inv *inv = hwinv_new();
    if (what == FXH_BCM || what == FXH_BCM_ETH) {
        hwinv_add_dev(inv, "Net", "PCI\\VEN_14E4&DEV_43B1&SUBSYS_0608105B&REV_03",
                      "PCI\\VEN_14E4&DEV_43B1&CC_028000;PCI\\VEN_14E4&DEV_43B1&CC_0280",
                      "Broadcom 802.11ac wireless", "Broadcom", "bcmwl63a", 0, 71);
        inv->devs[0].in_use = 1;
    } else {
        hwinv_add_dev(inv, "Net", "PCI\\VEN_8086&DEV_06F0&SUBSYS_00748086&REV_00", "PCI\\VEN_8086&DEV_06F0&CC_028000",
                      "Intel(R) Wi-Fi 6 AX201 160MHz", "Intel", "Netwtw10", 0, 71);
    }
    if (what == FXH_BCM_ETH)
        hwinv_add_dev(inv, "Net", "PCI\\VEN_10EC&DEV_8168&SUBSYS_8AD01043&REV_15", "PCI\\VEN_10EC&DEV_8168&CC_020000",
                      "Realtek PCIe GbE Family Controller", "Realtek", "rt640x64", 0, 6);
    if (what == FXH_CAMERA)
        hwinv_add_dev(inv, "System", "PCI\\VEN_8086&DEV_9A19&SUBSYS_22128086", "PCI\\VEN_8086&DEV_9A19&CC_048000",
                      "Intel(R) Image Signal Processor", "", "", 0, 0);
    hwcompat_judge(inv);
    hw_view_from(&g_hw, inv);
    hw_add_stop(&g_report, &g_hw);
    hwinv_free(inv);
}

static void shot_save(const char *dir, const char *name)
{
    if (!g_shot_mode) return;          /* harness only, never the UI path */
    char p[MAX_PATH + 64];
    _snprintf(p, sizeof p - 1, "%s/%s.bmp", dir, name);
    p[sizeof p - 1] = 0;
    render();
    render();                 /* second pass settles deferred focus */
    save_bmp(p);
}

static void shot_size(int w, int h, int dpi)
{
    g_dpi = dpi; g_cw = w; g_ch = h;
    backbuffer(g_cw, g_ch);
    fonts_make();
}

static int shot_run(const char *dir)
{
    g_shot_mode = 1;
    /* A 1366x768 laptop, or a 1920x1080 one at 150%: the window's
     * client area is about 1120x690 on both. */
    shot_size(1120, 690, 96);
    if (!g_px) return 2;
    detect_defaults();
    ab_lock_init();

    g_page = PAGE_WELCOME;
    memset(&g_report, 0, sizeof g_report);
    g_pf_valid = 0; g_checking = 1; g_ticks = 5;
    shot_save(dir, "1a-welcome-checking");

    fx_machine(0); fx_ready();
    g_focus = -1; g_want_focus_id = 0;
    InterlockedExchange(&g_dl_state, 1);            /* the page the check ends on says AurOS is downloading */
    shot_save(dir, "1c-welcome-ready");
    InterlockedExchange(&g_dl_state, 0);

    fx_machine(0);
    fx_add("removable-attached", PF_WARN, "A USB drive is plugged in");
    fx_ready();
    shot_save(dir, "1d-welcome-good-to-know");

    fx_machine(0);
    fx_add("not-on-ac", PF_BLOCK, "This computer is running on battery");
    fx_add("insufficient-space", PF_BLOCK, "Not enough free space");
    fx_add("pending-reboot", PF_BLOCK, "Windows has updates waiting for a restart");
    g_temp_bytes = 2300000000ULL;
    g_page = PAGE_BLOCKED; shot_save(dir, "2a-blocked-fixable");
    g_temp_bytes = 0;

    fx_machine(0);
    fx_add("firmware-bios", PF_BLOCK, "This computer uses legacy BIOS boot");
    fx_add("not-on-ac", PF_BLOCK, "This computer is running on battery");
    g_page = PAGE_BLOCKED; shot_save(dir, "2b-blocked-bios");

    fx_machine(0);
    fx_add("arm-pc", PF_BLOCK, "This computer has an ARM processor");
    g_page = PAGE_BLOCKED; shot_save(dir, "2e-blocked-arm");

    fx_machine(0);
    fx_add("intel-rst", PF_BLOCK, "AurOS can't see this computer's drive");
    g_page = PAGE_BLOCKED; shot_save(dir, "2f-blocked-intel-nvme");

    fx_machine(1);
    fx_add("bitlocker-system", PF_BLOCK, "This drive is encrypted with BitLocker");
    g_page = PAGE_BLOCKED; shot_save(dir, "2g-blocked-encrypted-hard-disk");

    /* A Secured-core PC, which trusts nothing but Windows: the one
     * refusal that carries a picture, judged by the same sbdb_judge()
     * preflight calls. */
    memset(&g_report, 0, sizeof g_report);
    g_report.system_disk = -1;
    sbdb_judge(&g_report, 1, 0, (const unsigned char *)"", 0, -1, NULL, 0,
               "Microsoft Corporation UEFI CA 2011\n");
    g_page = PAGE_BLOCKED; shot_save(dir, "2c-blocked-secure-boot");
    g_sb_helper = 1;
    shot_save(dir, "2d-blocked-secure-boot-helper");
    g_scroll[PAGE_BLOCKED] = 10000;
    shot_save(dir, "2d-blocked-secure-boot-helper-end");
    g_scroll[PAGE_BLOCKED] = 0;
    g_sb_helper = 0;

    fx_machine(0); fx_ready();
    g_page = PAGE_STYLE; g_sel_style = STYLE_DEFAULT;
    shot_save(dir, "3a-style");
    g_sel_style = 1;
    g_page = PAGE_STYLE; g_focus = -1; g_want_focus_id = ID_STYLE + 1; g_focus_ring = 1;
    shot_save(dir, "3b-style-mac-keyboard");
    g_focus = -1; g_focus_ring = 0; g_sel_style = STYLE_DEFAULT;

    g_page = PAGE_READY; shot_save(dir, "4a-ready");
    /* reading the Terms, and the Licenses, inside the window */
    g_pane = 1; g_scroll[PAGE_COUNT] = 0; shot_save(dir, "4e-ready-terms");
    g_scroll[PAGE_COUNT] = S(1500); shot_save(dir, "4f-ready-terms-scrolled");
    g_pane = 2; g_scroll[PAGE_COUNT] = S(2400); shot_save(dir, "4g-licenses");
    g_pane = 0; g_scroll[PAGE_COUNT] = 0;
    fx_machine(0); fx_ready(); fx_hw(FXH_CAMERA);
    g_page = PAGE_READY; shot_save(dir, "4d-ready-hardware-line");
    fx_machine(0); fx_ready(); fx_hw(FXH_BCM_ETH);
    g_page = PAGE_WELCOME; g_pf_valid = 1; g_checking = 0; shot_save(dir, "1e-welcome-wifi-note");
    fx_machine(0); fx_ready(); fx_hw(FXH_BCM);
    g_page = PAGE_BLOCKED; shot_save(dir, "2h-blocked-no-internet");
    fx_machine(0); fx_ready();
#if defined(AB_CHOICE_BRING_WIFI) && defined(AB_CHOICE_BITLOCKER_PAUSE)
    /* only when the engine can carry them (A2): her Wi-Fi, and the drive
     * lock paused for one restart (D10, D11, D13) */
    g_report.wifi_connected = 1;
    snprintf(g_report.wifi_name, sizeof g_report.wifi_name, "HomeNet");
    g_report.volumes[0].bitlocker = 1;
    g_page = PAGE_READY; shot_save(dir, "4c-ready-wifi-lock");
    g_report.wifi_connected = 0; g_report.volumes[0].bitlocker = 0;
#endif
    fx_machine(1); fx_ready(); g_sel_style = 2;
    g_page = PAGE_READY; shot_save(dir, "4b-ready-hard-disk");
    fx_machine(0); fx_ready(); g_sel_style = STYLE_DEFAULT;

    g_page = PAGE_PROGRESS;
    g_install_running = 1; g_install_finished = 0;
    g_ab_phase = AB_PREPARE; g_ab_pct = 41;
    g_eta_min = 12;
    shot_save(dir, "5a-progress");
    g_ab_phase = AB_INSPECT; g_ab_pct = 0; g_eta_min = -1; g_bar_f = 0.f;
    shot_save(dir, "5b-progress-starting");
    g_confirm = 1;
    shot_save(dir, "5f-stop-question");
    g_confirm = 0;

    g_install_running = 0; g_install_finished = 1;
    InterlockedExchange(&g_ab_state, 2);
    shot_save(dir, "5c-ready-to-restart");
    g_install_armed = 1; g_confirm = 2;
    shot_save(dir, "5g-close-question-ready");
    g_confirm = 0; g_install_armed = 0;

    InterlockedExchange(&g_ab_state, 3);
    snprintf(g_ab_why, sizeof g_ab_why,
             "part 7 of AurOS could not be downloaded intact. Check that the "
             "internet is working, then press Try again.");
    shot_save(dir, "5e-stopped");
    InterlockedExchange(&g_ab_state, 0);
    g_install_finished = 0;

    /* The main pages again on the other screens she may have: a
     * 1920x1080 laptop at 150% (every shape drawn at its real size), a
     * 1024x600 netbook, and a 1366x768 laptop at 150% -- the tightest. */
    static const struct { const char *pre; int w, h, dpi; } SZ[] = {
        { "hidpi", 1680, 1035, 144 }, { "small", 1024, 530, 96 },
        { "tight", 1366, 663, 144 },
    };
    for (int k = 0; k < 3; k++) {
        char n[64];
        shot_size(SZ[k].w, SZ[k].h, SZ[k].dpi);
        fx_machine(0); fx_ready(); g_sel_style = STYLE_DEFAULT;
        g_page = PAGE_WELCOME; snprintf(n, sizeof n, "%s-1c-welcome-ready", SZ[k].pre); shot_save(dir, n);
        g_page = PAGE_STYLE;   snprintf(n, sizeof n, "%s-3a-style", SZ[k].pre);         shot_save(dir, n);
        g_page = PAGE_READY;   snprintf(n, sizeof n, "%s-4a-ready", SZ[k].pre);         shot_save(dir, n);
        g_page = PAGE_PROGRESS;
        g_install_running = 1; g_install_finished = 0;
        g_ab_phase = AB_PREPARE; g_ab_pct = 41; g_eta_min = 12;
        snprintf(n, sizeof n, "%s-5a-progress", SZ[k].pre); shot_save(dir, n);
        g_install_running = 0; g_install_finished = 1;
        InterlockedExchange(&g_ab_state, 2);
        snprintf(n, sizeof n, "%s-5c-ready-to-restart", SZ[k].pre); shot_save(dir, n);
        InterlockedExchange(&g_ab_state, 0);
        g_install_finished = 0; g_eta_min = -1; g_bar_f = 0.f;
        fx_machine(0);
        fx_add("not-on-ac", PF_BLOCK, "This computer is running on battery");
        fx_add("insufficient-space", PF_BLOCK, "Not enough free space");
        fx_add("pending-reboot", PF_BLOCK, "Windows has updates waiting for a restart");
        g_temp_bytes = 2300000000ULL;
        g_page = PAGE_BLOCKED; snprintf(n, sizeof n, "%s-2a-blocked-fixable", SZ[k].pre); shot_save(dir, n);
        g_temp_bytes = 0;
    }
    return 0;
}

static FILE *g_nt;
static int   g_nt_fail;

static void nt_check(int ok, const char *what)
{
    if (!ok) g_nt_fail++;
    if (g_nt) fprintf(g_nt, "  [%s] %s\n", ok ? "PASS" : "FAIL", what);
}

static void nt_fake_report(int blocked)     /* test fixture, not a real scan */
{
    fx_machine(0);
    if (blocked) fx_add("test-block", PF_BLOCK, "synthetic blocking issue");
    else fx_ready();
}

/* Every widget on the page in front of it is a button or a picture
 * card, at least 44 px on its short side at 96 dpi (docs/EASY.md,
 * rule 4). A page that asks her to type, or to tick a box, fails. */
static int nt_page_is_plain(void)
{
    render();
    for (int i = 0; i < g_nw; i++) {
        if (g_w[i].kind != W_BUTTON && g_w[i].kind != W_CARD &&
            !(g_w[i].kind == W_TOGGLE && g_w[i].id == ID_WIFI)) return 0;
        int w = g_w[i].r.right - g_w[i].r.left, h = g_w[i].r.bottom - g_w[i].r.top;
        if ((w < h ? w : h) < S(44)) return 0;
    }
    return 1;
}

static int nav_test(const char *dir)
{
    char path[MAX_PATH + 32];
    _snprintf(path, sizeof path - 1, "%s/navtest.log", dir);
    path[sizeof path - 1] = 0;
    g_nt = fopen(path, "w");

    g_nav_mode = 1;
    g_dpi = 96; g_cw = 1120; g_ch = 690;
    backbuffer(g_cw, g_ch);
    fonts_make();
    detect_defaults();
    ab_lock_init();

    if (g_nt) fprintf(g_nt, "AurBridge wizard - navigation gate test\n\n"
                            "1. preflight has not run yet\n");
    g_pf_valid = 0;
    memset(&g_report, 0, sizeof g_report);
    for (int p = (int)PAGE_STYLE; p < (int)PAGE_COUNT; p++) {
        g_page = PAGE_WELCOME;
        goto_page((page_id)p);
        nt_check(!nav_allowed((page_id)p) && g_page == PAGE_WELCOME,
                 "page refused before preflight has run");
    }
    nt_check(!primary_enabled(), "Next waits for the check");

    if (g_nt) fprintf(g_nt, "\n2. preflight blocked\n");
    nt_fake_report(1);
    for (int p = (int)PAGE_STYLE; p < (int)PAGE_COUNT; p++) {
        g_page = PAGE_BLOCKED;
        goto_page((page_id)p);
        nt_check(!nav_allowed((page_id)p) && g_page == PAGE_BLOCKED,
                 "page refused while a block stands");
    }
    g_page = PAGE_WELCOME;
    check_advance();
    nt_check(g_page == PAGE_BLOCKED, "a blocked check lands on the stop page");
    g_page = PAGE_READY;
    g_check_next = PAGE_PROGRESS;          /* as if launched from Install AurOS */
    check_advance();
    nt_check(g_page == PAGE_BLOCKED && !g_install_running,
             "a re-check before install refuses instead of starting phases");
    nt_fake_report(0);
    g_check_next = PAGE_PROGRESS;
    blocked_cleared();
    nt_check(g_page == PAGE_WELCOME && !g_install_running && g_check_next == PAGE_STYLE,
             "a stop that clears by itself goes back to the first page, never into the install");

    if (g_nt) fprintf(g_nt, "\n3. preflight clean: one press per page, nothing typed\n");
    nt_fake_report(0);
    g_page = PAGE_WELCOME; g_sel_style = STYLE_DEFAULT;
    nt_check(primary_enabled() && !wcscmp(primary_label(), L"Next"),
             "the first page offers Next once the check is clean");
    on_key(VK_RETURN);
    nt_check(g_page == PAGE_STYLE, "Enter presses Next: the look page");
    nt_check(primary_enabled() && !wcscmp(primary_label(), L"Next"),
             "a look is already chosen, so Next works straight away");
    on_key(VK_SPACE);
    nt_check(g_page == PAGE_READY, "Space presses Next too: the Ready page");
    nt_check(primary_enabled() && !wcscmp(primary_label(), L"Install AurOS"),
             "Ready's one button is Install AurOS, with nothing to tick or type first");
    nt_check(nav_allowed(PAGE_PROGRESS), "the install is reachable from Ready as it stands");
    g_page = PAGE_STYLE;                   /* she went Back while it checked */
    g_check_next = PAGE_PROGRESS;
    check_advance();
    nt_check(g_page == PAGE_STYLE && !g_install_running,
             "a clean re-check after she went Back starts nothing");
    g_page = PAGE_READY;
    g_check_next = PAGE_PROGRESS;
    check_advance();                       /* what the re-check does when clean */
    nt_check(g_page == PAGE_PROGRESS, "a clean re-check starts the install");
    g_page = PAGE_WELCOME;

    if (g_nt) fprintf(g_nt, "\n3b. one press is one press: a double-click never skips Ready\n");
    {
        nt_fake_report(0);
        g_check_next = PAGE_STYLE;
        g_nt_auto = 0;
        g_nt_clock += 5000;
        goto_page(PAGE_STYLE);
        g_nt_clock += 2000;
        widget_activate(ID_PRIMARY);           /* Next, by mouse */
        g_nt_clock += 300;
        render();
        widget_activate(ID_PRIMARY);           /* ... and again, 300 ms later */
        nt_check(g_page == PAGE_READY && !g_checking && g_check_next != PAGE_PROGRESS,
                 "two clicks on Next within 300 ms land on Ready, not on the install");
        g_nt_clock += 300;
        on_key(VK_RETURN);                     /* Enter, 600 ms after the page came */
        nt_check(g_page == PAGE_READY && !g_checking,
                 "Enter within 700 ms of Ready appearing does nothing");
        goto_page(PAGE_STYLE);
        g_nt_clock += 2000;
        on_key(VK_RETURN);
        g_nt_clock += 200;
        on_key(VK_RETURN);
        nt_check(g_page == PAGE_READY && !g_checking,
                 "two Enters within 200 ms land on Ready, not on the install");
        g_nt_clock += 2000;
        nt_check(press_allowed(ID_PRIMARY) && !press_allowed(ID_PRIMARY),
                 "after the page has stood still, one press goes through, its echo does not");
        g_nt_auto = 1;
        g_check_next = PAGE_STYLE;
        g_page = PAGE_WELCOME;
    }

    if (g_nt) fprintf(g_nt, "\n4. nothing to type, nothing to tick, nothing small\n");
    {
        static const struct { page_id p; const char *name; } P[] = {
            { PAGE_WELCOME, "welcome" }, { PAGE_STYLE, "look" },
            { PAGE_READY, "ready" }, { PAGE_PROGRESS, "progress" },
        };
        for (size_t i = 0; i < sizeof P / sizeof P[0]; i++) {
            char what[128];
            nt_fake_report(0);
            g_page = P[i].p;
            snprintf(what, sizeof what, "the %s page has only buttons and pictures, all >= 44 px",
                     P[i].name);
            nt_check(nt_page_is_plain(), what);
            snprintf(what, sizeof what, "the %s page fits a 1366x768 laptop without scrolling",
                     P[i].name);
            nt_check(g_content_h <= g_view_h, what);
        }
        nt_fake_report(1);
        g_page = PAGE_BLOCKED;
        nt_check(nt_page_is_plain(), "the stop page has only buttons");
        g_page = PAGE_PROGRESS;
        g_install_finished = 1;
        InterlockedExchange(&g_ab_state, 2);
        nt_check(nt_page_is_plain(), "the restart page has only buttons");
        nt_check(g_content_h <= g_view_h, "the restart page fits without scrolling");
        nt_check(!wcscmp(primary_label(), L"Restart now"), "its big button is Restart now");
        g_install_finished = 0;
        InterlockedExchange(&g_ab_state, 0);
    }

    if (g_nt) fprintf(g_nt, "\n4b. every page fits the small screens too, and all four looks show\n");
    {
        /* A 1024x600 netbook, and a 1366x768 laptop at 150%: the window's
         * client area, in pixels and dots per inch. */
        static const struct { int w, h, dpi; const char *name; } SZ[] = {
            { 1024, 530, 96, "a 1024x600 netbook" }, { 1366, 663, 144, "a 1366x768 laptop at 150%" },
            { 1680, 1035, 144, "a 1920x1080 laptop at 150%" },
        };
        static const struct { page_id p; int done; const char *name; } P[] = {
            { PAGE_WELCOME, 0, "welcome" }, { PAGE_STYLE, 0, "look" },
            { PAGE_READY, 0, "ready" }, { PAGE_PROGRESS, 0, "progress" },
            { PAGE_PROGRESS, 1, "restart" },
        };
        for (size_t k = 0; k < sizeof SZ / sizeof SZ[0]; k++) {
            g_dpi = SZ[k].dpi; g_cw = SZ[k].w; g_ch = SZ[k].h;
            backbuffer(g_cw, g_ch);
            fonts_make();
            for (size_t i = 0; i < sizeof P / sizeof P[0]; i++) {
                char what[160];
                nt_fake_report(0);
                g_page = P[i].p;
                g_install_finished = P[i].done;
                InterlockedExchange(&g_ab_state, P[i].done ? 2 : 0);
                g_scroll[scr_i()] = 0;
                /* the first page as she sees it: the download is running, and says so */
                InterlockedExchange(&g_dl_state, P[i].p == PAGE_WELCOME ? 1 : 0);
                render();
                InterlockedExchange(&g_dl_state, 0);
                if (P[i].p == PAGE_READY && g_short) {
                    /* the Terms' risk and agreement (stream LEGAL) are more words than a
                     * small screen holds: Ready may scroll there, but the risk is its first
                     * row and the agreement and Install AurOS stay in view beside it */
                    int pr = -1, tl = -1;
                    for (int j = 0; j < g_nw; j++) {
                        if (g_w[j].id == ID_PRIMARY) pr = j;
                        if (g_w[j].id == ID_TERMS) tl = j;
                    }
                    snprintf(what, sizeof what,
                             "the ready page on %s may scroll, but Install AurOS and its agreement stay in view",
                             SZ[k].name);
                    nt_check(pr >= 0 && tl >= 0 && g_w[pr].r.bottom <= g_ch && g_w[tl].r.bottom <= g_w[pr].r.top
                             && g_w[tl].r.top >= g_body.bottom - S(60) && g_scroll[scr_i()] == 0, what);
                } else {
                    snprintf(what, sizeof what, "the %s page fits %s without scrolling",
                             P[i].name, SZ[k].name);
                    nt_check(g_content_h <= g_view_h, what);
                }
                if (P[i].p == PAGE_STYLE) {
                    int cards = 0;
                    for (int j = 0; j < g_nw; j++)
                        if (g_w[j].kind == W_CARD && g_w[j].r.bottom <= g_body.bottom &&
                            g_w[j].r.top >= g_body.top) cards++;
                    snprintf(what, sizeof what, "all four looks are on the screen on %s",
                             SZ[k].name);
                    nt_check(cards == N_STYLES, what);
                }
            }
            g_install_finished = 0;
            InterlockedExchange(&g_ab_state, 0);
        }
        g_dpi = 96; g_cw = 1120; g_ch = 690;
        backbuffer(g_cw, g_ch);
        fonts_make();
        render();
    }

    if (g_nt) fprintf(g_nt, "\n5. the look she picks reaches the installed system\n");
    {
        nt_fake_report(0);
        nt_check(!strcmp(STYLES[STYLE_DEFAULT].shell, "taskbar"),
                 "the one chosen to begin with is the one like Windows");
        /* The website promises these four, by these names. */
        static const wchar_t *const promised[] = { L"Like Windows", L"Like a Mac",
                                                   L"Big and simple", L"One thing at a time" };
        int names = N_STYLES == 4;
        for (int i = 0; names && i < 4; i++) names = !wcscmp(STYLES[i].name, promised[i]);
        nt_check(names, "the four looks carry the names the website shows");
        int ok = 1;
        for (int i = 0; i < N_STYLES; i++) {
            ab_choice c;
            memset(&c, 0, sizeof c);
            g_page = PAGE_STYLE;
            render();
            widget_activate(ID_STYLE + i);         /* a click on its picture */
            choices_from_page(&c);
            char what[160];
            snprintf(what, sizeof what, "card %d (%s, %s) travels as shell=%s theme=%s",
                     i, STYLES[i].shell, STYLES[i].theme, c.shell_archetype, c.theme);
            int one = g_sel_style == i && c.shell_archetype[0] && c.theme[0] &&
                      !strcmp(c.shell_archetype, STYLES[i].shell) &&
                      !strcmp(c.theme, STYLES[i].theme);
            nt_check(one, what);
            ok &= one;
        }
        g_sel_style = 0;
        g_page = PAGE_STYLE;
        on_key(VK_RIGHT);
        nt_check(g_sel_style == 1, "the arrow keys move the choice");
        on_key(VK_LEFT); on_key(VK_LEFT);
        nt_check(g_sel_style == 0, "and stop at the first one");
        g_sel_style = STYLE_DEFAULT;
        (void)ok;
    }
    {
        detect_defaults();
        int es = -1, kes = -1, lon = -1;
        for (int i = 0; i < g_n_langs; i++) if (!wcscmp(g_langs[i], L"Español")) es = i;
        for (int i = 0; i < g_n_kbds; i++)  if (!wcscmp(g_kbds[i], L"Spanish")) kes = i;
        for (int i = 0; i < g_n_tzs; i++)   if (!wcscmp(g_tzs[i], L"London")) lon = i;
        nt_check(es >= 0 && kes >= 0 && lon >= 0, "the language values are all known");
        if (es >= 0 && kes >= 0 && lon >= 0) {
            g_sel_lang = es; g_sel_kbd = kes; g_sel_tz = lon;
            ab_choice c;
            memset(&c, 0, sizeof c);
            choices_from_page(&c);
            nt_check(!strcmp(c.language, "es_ES.UTF-8"), "Español travels as es_ES.UTF-8");
            nt_check(!strcmp(c.keyboard, "xkb:es"), "the Spanish keyboard travels as xkb:es");
            nt_check(!strcmp(c.timezone, "iana:Europe/London"), "London travels as Europe/London");
        }
        g_sel_lang = g_sel_kbd = g_sel_tz = 0;
        ab_choice c;
        memset(&c, 0, sizeof c);
        choices_from_page(&c);
        nt_check(!strcmp(c.language, g_lang_v[0]) && !strcmp(c.timezone, g_tz_v[0]),
                 "with no question asked, what Windows uses is what travels");
    }

    if (g_nt) fprintf(g_nt, "\n6. a block appears after the user answered everything\n");
    nt_fake_report(1);
    for (int p = (int)PAGE_STYLE; p < (int)PAGE_COUNT; p++)
        nt_check(!nav_allowed((page_id)p), "every page past the first closes again");

    if (g_nt) fprintf(g_nt, "\n7. the stop page fixes what it can, in a few words\n");
    {
        int ok = 1, n = 0;
        for (const fix_info *f; (f = fix_at(n)) != NULL; n++) {
            if (!f->problem[0] || !f->fix[0] || wcslen(f->problem) > 60 || wcslen(f->fix) > 90)
                ok = 0;
            if (f->kind == FIX_AUTO && (!f->after || !f->after[0])) ok = 0;
            if (f->kind == FIX_UNKNOWN) ok = 0;
        }
        nt_check(ok && n > 10, "every known stop has one short line, and one short fix");
        static const char *warns[] = { "removable-attached", "low-ram", "smart-reallocated",
                                       "dynamic-disk-other", "storage-spaces-other",
                                       "fde-container-software", NULL };
        int wok = 1;
        for (int i = 0; warns[i]; i++) {
            const wchar_t *l = fix_note(warns[i]);
            if (!l || !l[0] || wcslen(l) > 110) wok = 0;
        }
        nt_check(wok, "every warning the check can give has one short line");
        /* D11: BitLocker is paused for one restart, NEVER decrypted. Until
         * the engine can pause it, an encrypted drive is a plain stop: no
         * button may switch encryption off (it once ran manage-bde -off on
         * every fixed drive). Asked of the table, never by running a fix. */
        const fix_info *b1 = fix_lookup("bitlocker-system"), *b2 = fix_lookup("bitlocker-other");
        nt_check(b1 && b1->kind == FIX_CANNOT && b2 && b2->kind == FIX_CANNOT,
                 "an encrypted drive is a stop: nothing here decrypts it (D11)");
        fx_machine(0);
        fx_add("bitlocker-system", PF_BLOCK, "This drive is encrypted with BitLocker");
        g_page = PAGE_BLOCKED;
        nt_check(fixable_now() == 0 && !wcscmp(primary_label(), L"Close AurOS Setup"),
                 "on an encrypted drive the one button is Close");
        /* THE ONLY FOLDERS SWEPT are named, never read from TEMP or TMP:
         * those can point at her documents. Point them there and ask. */
        {
            wchar_t keep[MAX_PATH], roots[2][MAX_PATH], la[MAX_PATH];
            GetEnvironmentVariableW(L"TEMP", keep, MAX_PATH);          /* to put back */
            SetEnvironmentVariableW(L"TEMP", L"C:\\Users\\her\\Documents");
            SetEnvironmentVariableW(L"TMP",  L"C:\\Users\\her\\Documents");
            int n = fix_temp_roots(roots);
            SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, la);
            int ok = n == 2 && !_wcsnicmp(roots[0], la, wcslen(la)) &&
                     !_wcsicmp(roots[0] + wcslen(la), L"\\Temp") &&
                     !_wcsicmp(roots[1] + wcslen(roots[1]) - 5, L"\\Temp") &&
                     !wcsstr(roots[0], L"Documents") && !wcsstr(roots[1], L"Documents");
            nt_check(ok, "the space fix sweeps LocalAppData\\Temp and Windows\\Temp by name, "
                         "never the folder TEMP points at (her documents)");
            SetEnvironmentVariableW(L"TEMP", keep[0] ? keep : NULL);
            SetEnvironmentVariableW(L"TMP", NULL);
        }
        /* The space fix deletes only old temporary files, and says how
         * much before she presses; with next to nothing to delete there
         * is no Fix button, only what she can move. */
        fx_machine(0);
        fx_add("insufficient-space", PF_BLOCK, "Not enough free space");
        g_temp_bytes = 2300000000ULL;
        wchar_t line[200];
        space_fix_line(line, 200);
        nt_check(wcsstr(line, L"2.3 GB") && fixable_now() == 1 &&
                 !wcscmp(primary_label(), L"Fix these for me"),
                 "the space fix says what it deletes, 2.3 GB, before she presses");
        g_temp_bytes = 20000000ULL;
        space_fix_line(line, 200);
        nt_check(fixable_now() == 0 && wcsstr(line, L"Move big files"),
                 "with next to nothing to delete there is no Fix button, only what she can move");
        g_temp_bytes = 0;
    }
    {
        static const char *ids[] = { "not-on-ac", "insufficient-space", "removable-no-serial",
                                     "test-block", "firmware-bios" };
        fx_machine(0);
        g_temp_bytes = 2300000000ULL;      /* old temporary files to delete */
        for (int i = 0; i < 5; i++) fx_add(ids[i], PF_BLOCK, "a synthetic problem");
        g_report.n = 2 + 4; g_report.n_block = 4;
        g_page = PAGE_BLOCKED;
        nt_check(fixable_now() == 1 && !wcscmp(primary_label(), L"Fix these for me"),
                 "something a program can fix: the button fixes it");
        snprintf(g_report.results[3].id, sizeof g_report.results[3].id, "pending-reboot");
        nt_check(fixable_now() == 2, "a waiting restart is something the button does");
        g_scroll[PAGE_BLOCKED] = 0;
        render();
        nt_check(g_content_h > 0 && g_content_h <= g_view_h,
                 "four problems a program or she can fix fit on the page without scrolling");
        g_report.n = 2 + 5; g_report.n_block = 5;
        nt_check(fixable_now() == 0 && !wcscmp(primary_label(), L"Close AurOS Setup"),
                 "next to something unfixable, nothing is fixed, nothing restarts, "
                 "and the one button is Close");
        g_scroll[PAGE_BLOCKED] = 0;
        render();
        nt_check(g_content_h > 0 && g_content_h <= g_view_h,
                 "when it cannot go on, the reason and what to do instead fit without scrolling");
        g_view_h = 300; g_content_h = 900; g_scroll[PAGE_BLOCKED] = 0;
        scroll_by(10000);
        nt_check(g_scroll[PAGE_BLOCKED] == 600, "a long page scrolls to its end, and no further");
        g_scroll[PAGE_BLOCKED] = 0;
        g_temp_bytes = 0;
    }

    if (g_nt) fprintf(g_nt, "\n8. the restart: only when she presses Restart now; later is a window that waits\n");
    {
        nt_fake_report(0);
        g_page = PAGE_PROGRESS; g_install_finished = 1;
        InterlockedExchange(&g_ab_state, 2);
        g_nt_restarts = 0; g_nt_waits = 0;
        render();
        int later = 0;
        for (int i = 0; i < g_nw; i++) if (g_w[i].id == ID_LATER) later = 1;
        nt_check(later && !wcscmp(primary_label(), L"Restart now"),
                 "ready, it offers Restart now and Restart later");
        for (int i = 0; i < 2000; i++) tick();   /* two minutes of the clock */
        nt_check(!g_restarting && g_nt_restarts == 0 && g_page == PAGE_PROGRESS,
                 "nothing restarts by itself, however long it waits");
        on_key(VK_ESCAPE);
        nt_check(g_page == PAGE_PROGRESS && g_nt_restarts == 0, "Esc does nothing here");
        widget_activate(ID_LATER);
        nt_check(g_nt_waits == 1 && g_nt_restarts == 0,
                 "Restart later only waits (the window is minimised); it restarts nothing");
        widget_activate(ID_PRIMARY);
        nt_check(g_nt_restarts == 1, "Restart now is the one thing that restarts");
        g_nt_restarts = 0;
        g_install_finished = 0;
        InterlockedExchange(&g_ab_state, 0);
    }

    if (g_nt) fprintf(g_nt, "\n8b. a fix that restarts Windows asks her to save her work first\n");
    {
        fx_machine(0);
        fx_add("pending-reboot", PF_BLOCK, "Windows has updates waiting for a restart");
        g_page = PAGE_BLOCKED; g_confirm = 0; g_scroll[PAGE_BLOCKED] = 0;
        render();
        nt_check(fixable_now() == 2 && !wcscmp(primary_label(), L"Fix these for me"),
                 "a waiting restart is among the fixes");
        widget_activate(ID_PRIMARY);
        render();
        int not_yet = 0;
        for (int i = 0; i < g_nw; i++) if (g_w[i].id == ID_LEAVE) not_yet = 1;
        nt_check(g_confirm == 3 && !wcscmp(primary_label(), L"I have saved my work") && not_yet &&
                 !g_fix_busy,
                 "Fix these for me asks first: Save your work first, Restart Windows now / Not yet");
        widget_activate(ID_LEAVE);
        nt_check(g_confirm == 0 && !g_fix_busy && g_page == PAGE_BLOCKED, "Not yet changes nothing");
        fx_machine(0);
        fx_add("insufficient-space", PF_BLOCK, "Not enough free space");
        g_temp_bytes = 2300000000ULL;
        g_page = PAGE_BLOCKED; g_confirm = 0;
        render();
        widget_activate(ID_PRIMARY);
        nt_check(g_confirm == 0, "a fix that does not restart Windows does not ask");
        g_temp_bytes = 0;
        g_confirm = 0;
        g_pf_valid = 1;
    }

    if (g_nt) fprintf(g_nt, "\n9. Esc never closes anything\n");
    {
        nt_fake_report(0);
        g_page = PAGE_READY;   on_key(VK_ESCAPE);
        nt_check(g_page == PAGE_STYLE, "Esc on Ready goes back to the looks");
        on_key(VK_ESCAPE);
        nt_check(g_page == PAGE_WELCOME, "Esc on the looks goes back to the first page");
        on_key(VK_ESCAPE);
        nt_check(g_page == PAGE_WELCOME, "Esc on the first page does nothing");
        MSG msg;
        int closed = 0;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            if (msg.message == WM_CLOSE) closed = 1;
        nt_check(!closed, "no key asked the window to close");
    }

    if (g_nt) fprintf(g_nt, "\n10. how long, said before she says yes\n");
    {
        int lo, hi;
        fx_machine(0); fx_ready(); eta_range(&lo, &hi);
        char what[96];
        snprintf(what, sizeof what, "an SSD laptop: %d to %d minutes, a wide honest range", lo, hi);
        nt_check(lo >= 15 && hi >= 45 && hi <= 90 && lo < hi, what);
        fx_machine(1); fx_ready(); eta_range(&lo, &hi);
        snprintf(what, sizeof what, "a hard disk: %d to %d minutes, hours", lo, hi);
        nt_check(lo >= 45 && hi >= 120 && lo < hi, what);
        wchar_t rng[64];
        range_words(lo, hi, rng, 64);
        nt_check(wcsstr(rng, L"hours") != NULL, "...said in hours");
        int slo, shi;
        fx_machine(0); fx_ready(); eta_range(&slo, &shi);
        InterlockedExchange(&g_dl_state, 2);
        eta_range(&lo, &hi);
        snprintf(what, sizeof what, "with the download already done: %d to %d minutes", lo, hi);
        nt_check(hi < shi && lo >= 5, what);
        InterlockedExchange(&g_dl_state, 0);
    }

    if (g_nt) fprintf(g_nt, "\n12. her Wi-Fi: offered only when there is some, and when it travels\n");
    {
        nt_fake_report(0);
        g_page = PAGE_READY; g_bring_wifi = 1;
        render();
        int box = 0;
        for (int i = 0; i < g_nw; i++) if (g_w[i].id == ID_WIFI) box = 1;
        nt_check(!box, "not on Wi-Fi: no Wi-Fi box");
        g_report.wifi_connected = 1;
        snprintf(g_report.wifi_name, sizeof g_report.wifi_name, "HomeNet");
        g_report.volumes[0].bitlocker = 1;     /* and a locked drive: the longest Ready page */
        render();
        box = -1;
        for (int i = 0; i < g_nw; i++) if (g_w[i].id == ID_WIFI) box = i;
#ifdef AB_CHOICE_BRING_WIFI
        nt_check(box >= 0 && g_bring_wifi, "on Wi-Fi: the box is there, ticked for her");
        nt_check(box < 0 || (g_w[box].r.bottom - g_w[box].r.top) >= S(44),
                 "the whole row is a target at least 44 px tall");
        widget_activate(ID_WIFI);
        ab_choice c;
        memset(&c, 0, sizeof c);
        choices_from_page(&c);
        nt_check(!g_bring_wifi && c.bring_wifi == 0, "unticked, the engine is told not to");
        nt_check(primary_enabled() && nav_allowed(PAGE_PROGRESS),
                 "unticked, Install AurOS still works: the box never holds the way on");
        widget_activate(ID_WIFI);
        memset(&c, 0, sizeof c);
        choices_from_page(&c);
        nt_check(g_bring_wifi && c.bring_wifi == 1, "ticked again, the engine is told to");
        /* The Terms' risk and agreement (stream LEGAL) take the room this page
         * had: with the box AND the drive-lock line, the longest Ready, it
         * scrolls a little. The risk is still its first row, and Tab brings
         * the box into view. */
        g_scroll[PAGE_READY] = 0;
        render();
        for (int i = 0; i < g_nw; i++) if (g_w[i].id == ID_WIFI) g_focus = i;
        focus_visible();
        render();
        box = -1;
        for (int i = 0; i < g_nw; i++) if (g_w[i].id == ID_WIFI) box = i;
        nt_check(g_content_h <= g_view_h ||
                 (box >= 0 && g_w[box].r.top >= g_body.top && g_w[box].r.bottom <= g_body.bottom),
                 "with the box, Ready fits, or Tab brings the box into view");
        g_focus = -1;
        g_scroll[PAGE_READY] = 0;
#else
        nt_check(box < 0, "on Wi-Fi, but the engine cannot carry it yet: no box, no promise");
#endif
        g_report.wifi_connected = 0;
        g_report.volumes[0].bitlocker = 0;
        g_bring_wifi = 1;
    }

    if (g_nt) fprintf(g_nt, "\n13. closing in the middle of a step never cuts it off, and asks in the window\n");
    {
        nt_fake_report(0);
        g_page = PAGE_PROGRESS;
        g_install_running = 1; g_install_finished = 0;
        InterlockedExchange(&g_ab_state, 1);
        InterlockedExchange(&g_ab_gate, 1);    /* the engine is in a step */
        g_stop_wanted = 0; g_aborts = 0; g_confirm = 0;
        int may = close_asked(NULL);
        render();
        int has_leave = 0, has_keep = 0;
        for (int i = 0; i < g_nw; i++) { if (g_w[i].id == ID_LEAVE) has_leave = 1; if (g_w[i].id == ID_PRIMARY) has_keep = 1; }
        nt_check(!may && g_confirm == 1 && has_leave && has_keep && !wcscmp(primary_label(), L"Keep going"),
                 "closing mid-step asks in the window: Keep going, or Stop and undo");
        on_key(VK_ESCAPE);
        nt_check(g_confirm == 0 && g_page == PAGE_PROGRESS, "Esc cancels the question, and nothing else");
        close_asked(NULL);
        widget_activate(ID_LEAVE);             /* Stop and undo, mid-step */
        nt_check(g_stop_wanted && g_aborts == 0 && !stop_tick(),
                 "Stop and undo in the middle of a step does not end it: the window waits");
        InterlockedExchange(&g_ab_state, 2);    /* the step has ended */
        nt_check(stop_tick() && g_aborts == 1,
                 "when the step ends, everything is taken back, then the window goes");
        g_install_running = 1; g_stop_wanted = 0; g_aborts = 0;
        InterlockedExchange(&g_ab_state, 1);
        InterlockedExchange(&g_ab_gate, 0);    /* only the download, behind the pages */
        nt_check(!close_asked(NULL) && g_confirm == 1, "before the engine starts it asks too");
        nt_check(leave_confirmed() && g_ab_gate == 2,
                 "... and Stop and undo then closes at once, and the engine never starts");
        g_install_running = 1; g_stop_wanted = 0; g_confirm = 0;
        InterlockedExchange(&g_ab_gate, 0);
        render();
        int stop = 0;
        for (int i = 0; i < g_nw; i++) if (g_w[i].id == ID_STOP) stop = 1;
        nt_check(stop, "while only downloading there is a Stop button");
        InterlockedExchange(&g_ab_gate, 1);
        render();
        stop = 0;
        for (int i = 0; i < g_nw; i++) if (g_w[i].id == ID_STOP) stop = 1;
        nt_check(!stop, "once a step is running there is none: stopping is no longer free");
        /* Ready, and about to be thrown away */
        g_install_running = 0; g_install_finished = 1; g_install_armed = 1;
        InterlockedExchange(&g_ab_state, 2); InterlockedExchange(&g_ab_gate, 1);
        nt_check(!close_asked(NULL) && g_confirm == 2 && !wcscmp(primary_label(), L"Keep this window open"),
                 "closing a ready install asks: Keep this window open, or Close and undo");
        widget_activate(ID_PRIMARY);
        nt_check(g_confirm == 0 && g_nt_restarts == 0, "Keep this window open stays, and restarts nothing");
        g_install_armed = 0; g_install_finished = 0;
        g_install_running = 0; g_stop_wanted = 0; g_aborts = 0; g_confirm = 0;
        InterlockedExchange(&g_ab_state, 0);
        InterlockedExchange(&g_ab_gate, 0);
    }

    if (g_nt) fprintf(g_nt, "\n11. a screen reader can read every page and find its button\n");
    {
        static const page_id P[] = { PAGE_WELCOME, PAGE_STYLE, PAGE_READY, PAGE_PROGRESS };
        int named = 1, buttons = 1;
        for (size_t i = 0; i < sizeof P / sizeof P[0]; i++) {
            nt_fake_report(0);
            g_page = P[i];
            g_install_finished = (P[i] == PAGE_PROGRESS);
            InterlockedExchange(&g_ab_state, P[i] == PAGE_PROGRESS ? 2 : 0);
            render();
            VARIANT self; self.vt = VT_I4; self.lVal = CHILDID_SELF;
            BSTR s = NULL;
            if (g_acc.lpVtbl->get_accName(&g_acc, self, &s) != S_OK || !s || wcslen(s) < 10)
                named = 0;
            SysFreeString(s);
            int p = acc_primary_index();
            if (p < 0) { buttons = 0; continue; }
            VARIANT ch; ch.vt = VT_I4; ch.lVal = p + 1;
            VARIANT role; role.vt = VT_EMPTY;
            s = NULL;
            g_acc.lpVtbl->get_accName(&g_acc, ch, &s);
            g_acc.lpVtbl->get_accRole(&g_acc, ch, &role);
            if (!s || wcscmp(s, primary_label()) || role.lVal != ROLE_SYSTEM_PUSHBUTTON)
                buttons = 0;
            SysFreeString(s);
        }
        nt_check(named, "every page has a name a screen reader reads");
        nt_check(buttons, "every page's big button is a button, by its own label");
        nt_fake_report(0);
        g_page = PAGE_STYLE; g_sel_style = 1;
        render();
        int card = -1;
        for (int i = 0; i < g_nw; i++) if (g_w[i].id == ID_STYLE + 1) card = i;
        VARIANT ch; ch.vt = VT_I4; ch.lVal = card + 1;
        VARIANT st; st.vt = VT_EMPTY;
        BSTR s = NULL;
        g_acc.lpVtbl->get_accName(&g_acc, ch, &s);
        g_acc.lpVtbl->get_accState(&g_acc, ch, &st);
        nt_check(card >= 0 && s && !wcscmp(s, L"Like a Mac") && (st.lVal & STATE_SYSTEM_CHECKED),
                 "each look is read by its name, and the chosen one as chosen");
        SysFreeString(s);
        g_sel_style = STYLE_DEFAULT;
        g_install_finished = 0;
        InterlockedExchange(&g_ab_state, 0);
    }

    if (g_nt) fprintf(g_nt, "\n15. her hardware: no internet stops her; a cable makes it a note; a camera is one line\n");
    {
        /* a Broadcom BCM4352 (14e4:43b1) laptop with no cable socket: the case
         * the hardware check exists for */
        fx_machine(0); fx_ready();
        fx_hw(FXH_BCM);
        nt_check(g_report.n_block == 1 && !pf_is_go(&g_report) && !nav_allowed(PAGE_STYLE),
                 "a Wi-Fi card AurOS cannot drive and no cable socket: it stops before anything changes");
        g_page = PAGE_WELCOME; g_check_next = PAGE_STYLE;
        check_advance();
        nt_check(g_page == PAGE_BLOCKED, "... and the first page lands on the stop page");
        g_scroll[PAGE_BLOCKED] = 0;
        render();
        wchar_t nm[900];
        acc_page_name(nm, 900);
        nt_check(wcsstr(nm, L"AurOS would have no internet") && wcsstr(nm, L"no place for a network cable"),
                 "... which says it in HW's own words, the card's, and that nothing was changed");
        int has_help = 0, has_now = 0, has_rep = 0;
        for (int i = 0; i < g_nw; i++) { has_help |= g_w[i].id == ID_HELPER; has_now |= g_w[i].id == ID_QUIT; has_rep |= g_w[i].id == ID_REPORT; }
        nt_check(has_help && has_now && !has_rep && fixable_now() == 0 && !wcscmp(primary_label(), L"Check again"),
                 "... with Show a helper how (the stick list), Not now, and it checks again by itself");
        nt_check(fix_lookup("no-internet") && fix_lookup("no-internet")->kind == FIX_WAIT,
                 "... it is a stop she can clear herself (a stick), so the page waits and checks again");
        nt_check(fix_helper_url("no-internet") != NULL, "... and it has a helper page to open");

        /* the same card with a network cable socket: a note, not a stop */
        fx_machine(0); fx_ready();
        fx_hw(FXH_BCM_ETH);
        const wchar_t *ln[2]; static wchar_t bf[2][330];
        int notes = hw_internet_notes(ln, 2, bf);
        nt_check(g_report.n_block == 0 && pf_is_go(&g_report) && nav_allowed(PAGE_STYLE),
                 "the same card with a cable socket does not stop her");
        nt_check(notes == 1 && wcsstr(ln[0], L"network cable will"),
                 "... it is one line under Good to know on the first page");
        g_page = PAGE_WELCOME; g_pf_valid = 1; g_checking = 0; g_scroll[PAGE_WELCOME] = 0;
        render();
        nt_check(g_content_h <= g_view_h, "... and the first page still fits");

        /* an Intel AX201: nothing to say at all */
        fx_machine(0); fx_ready();
        fx_hw(FXH_AX);
        wchar_t hl[420];
        nt_check(g_report.n_block == 0 && hw_internet_notes(ln, 2, bf) == 0 && !hw_ready_line(hl, 420),
                 "an Intel AX201: nothing stops her and nothing is said");

        /* a camera that will not work yet: one line on Ready, never a stop */
        fx_machine(0); fx_ready();
        fx_hw(FXH_CAMERA);
        nt_check(g_report.n_block == 0 && nav_allowed(PAGE_READY) && nav_allowed(PAGE_PROGRESS),
                 "a camera that will not work yet never stops her");
        nt_check(hw_ready_line(hl, 420) && wcsstr(hl, L"your camera"), "... it is one line, naming it");
        g_page = PAGE_READY; g_scroll[PAGE_READY] = 0;
        render();
        nt_check(g_content_h <= g_view_h, "... and Ready still fits 1366x768 without scrolling");
        wchar_t rn[900];
        acc_page_name(rn, 900);
        nt_check(wcsstr(rn, L"Ready to install") != NULL, "... and Ready is still Ready");

        /* the can't-install page offers the report for a helper */
        fx_machine(0);
        fx_add("firmware-bios", PF_BLOCK, "This computer uses legacy BIOS boot");
        g_page = PAGE_BLOCKED; g_scroll[PAGE_BLOCKED] = 0;
        render();
        has_rep = 0;
        for (int i = 0; i < g_nw; i++) has_rep |= g_w[i].id == ID_REPORT;
        nt_check(has_rep && any_cannot(), "the can't-install page has Save a report for a helper");
        g_nt_reports = 0;
        widget_activate(ID_REPORT);
        nt_check(g_nt_reports == 1, "... and pressing it saves the report (the harness only counts)");
        fx_machine(0);
        fx_add("not-on-ac", PF_BLOCK, "This computer is running on battery");
        g_page = PAGE_BLOCKED; render();
        has_rep = 0;
        for (int i = 0; i < g_nw; i++) has_rep |= g_w[i].id == ID_REPORT;
        nt_check(!has_rep, "... but a page that fixes itself does not");
        g_hw_new = g_hw; memset(&g_hw, 0, sizeof g_hw);
    }

    if (g_nt) fprintf(g_nt, "\n16. the Terms and the Licenses: agreed to before Install, readable with no internet\n");
    {
        /* the texts travel inside the program (build/aurbridge stages them from
         * docs/legal/notices/; RCDATA 200 and 202) */
        wchar_t *terms = res_wide(RES_TERMS), *lic = res_wide(RES_NOTICES);
        nt_check(terms && wcsstr(terms, L"AUROS TERMS") && wcslen(terms) > 5000,
                 "the Terms are inside the program (resource 202)");
        nt_check(lic && wcsstr(lic, L"LICENSES AND NOTICES") && wcslen(lic) > 100000,
                 "the licences and notices are inside the program (resource 200)");
        nt_check(lic && !wcsstr(lic, L"@AUROS_") && terms && !wcsstr(terms, L"@AUROS_"),
                 "... with no @AUROS_ placeholder left in either");
        free(terms); free(lic);

        fx_machine(0); fx_ready();
        g_page = PAGE_READY; g_scroll[PAGE_READY] = 0; g_confirm = 0; g_pane = 0; g_checking = 0;
        g_nt_auto = 1;
        render();
        int pr = -1, tl = -1, ll = -1;
        for (int i = 0; i < g_nw; i++) {
            if (g_w[i].id == ID_PRIMARY)  pr = i;
            if (g_w[i].id == ID_TERMS)    tl = i;
            if (g_w[i].id == ID_LICENSES) ll = i;
        }
        nt_check(pr >= 0 && !wcscmp(primary_label(), L"Install AurOS") && g_page == PAGE_READY,
                 "Ready's one big button is still Install AurOS");
        nt_check(tl >= 0 && g_w[tl].name && !wcscmp(g_w[tl].name, L"Read the Terms"),
                 "Ready has the button Read the Terms");
        nt_check(pr >= 0 && tl >= 0 && g_w[tl].r.right == g_w[pr].r.right && g_w[tl].r.bottom <= g_w[pr].r.top
                 && g_w[pr].r.top - g_w[tl].r.bottom < S(40),
                 "... directly above Install AurOS, ending where it ends");
        nt_check(ll >= 0 && g_w[ll].name && !wcscmp(g_w[ll].name, L"Licenses"),
                 "Ready has the button Licenses, in the foot");
        wchar_t rn[1400];
        acc_page_name(rn, 1400);
        nt_check(wcsstr(rn, L"By pressing Install AurOS you agree to the AurOS Terms") != NULL,
                 "the screen reader reads the agreement");
        nt_check(wcsstr(rn, L"Any change to a drive carries some risk") != NULL &&
                 wcsstr(rn, L"might not come back") != NULL,
                 "... and the risk, before the button");
        ab_choice c;
        memset(&c, 0, sizeof c);
        choices_from_page(&c);
#ifdef AB_CHOICE_TERMS
        nt_check(!strcmp(c.terms_version, "v3") && c.terms_unix > 1700000000ULL,
                 "the engine is told which Terms she agreed to (v3) and when, for the install journal");
#endif

        /* Licenses is in the foot of every page */
        {
            static const struct { page_id p; int blocked; const char *name; } PG[] = {
                { PAGE_WELCOME, 0, "the first page" }, { PAGE_STYLE, 0, "the look page" },
                { PAGE_READY, 0, "Ready" }, { PAGE_PROGRESS, 0, "the install page" },
                { PAGE_BLOCKED, 1, "the stop page" },
            };
            for (size_t i = 0; i < sizeof PG / sizeof PG[0]; i++) {
                nt_fake_report(PG[i].blocked);
                g_page = PG[i].p; g_pane = 0; g_confirm = 0;
                render();
                int has = 0;
                for (int j = 0; j < g_nw; j++) has |= g_w[j].id == ID_LICENSES;
                char what[96];
                snprintf(what, sizeof what, "%s has Licenses", PG[i].name);
                nt_check(has, what);
            }
        }

        /* Read the Terms opens them in the window, and Esc comes back to Ready */
        fx_machine(0); fx_ready();
        g_page = PAGE_READY; g_scroll[PAGE_READY] = 0; g_pane = 0; g_confirm = 0;
        render();
        widget_activate(ID_TERMS);
        render();
        nt_check(g_pane == 1 && g_page == PAGE_READY, "Read the Terms opens the Terms in the window");
        nt_check(g_pl_n > 100 && g_pt && wcsstr(g_pt, L"AUROS TERMS") != NULL,
                 "... the text is there, wrapped to the window");
        int only_big = 1;
        for (int i = 0; i < g_nw; i++) only_big &= g_w[i].id == ID_PRIMARY;
        nt_check(only_big && !wcscmp(primary_label(), L"Done reading"),
                 "... nothing else on the screen can be pressed, and the one button is Done reading");
        nt_check(g_content_h > g_view_h, "... and it scrolls");
        scroll_by(S(100000));
        nt_check(g_scroll[scr_i()] == scroll_max() && g_scroll[PAGE_READY] == 0,
                 "... to its end, apart from Ready's own place");
        on_key(VK_ESCAPE);
        render();
        nt_check(g_pane == 0 && g_page == PAGE_READY && !g_checking && !wcscmp(primary_label(), L"Install AurOS"),
                 "Esc closes it, and Ready is as it was");
        /* the press that closes it must not also press what is under it */
        g_nt_auto = 0;
        g_nt_clock += 2000;
        widget_activate(ID_TERMS);
        g_nt_clock += 2000;
        widget_activate(ID_PRIMARY);                    /* Done reading */
        g_nt_clock += 600;                              /* past the repeat rule, inside the settle one */
        widget_activate(ID_PRIMARY);                    /* the echo of a slow double-click */
        nt_check(g_pane == 0 && g_page == PAGE_READY && !g_checking,
                 "a slow double-click on Done reading does not press Install AurOS");
        g_nt_auto = 1;

        /* Licenses, from the first page: the long text, with no internet */
        fx_machine(0); fx_ready();
        g_page = PAGE_WELCOME; g_scroll[PAGE_WELCOME] = 0; g_pane = 0;
        render();
        widget_activate(ID_LICENSES);
        render();
        nt_check(g_pane == 2 && g_pt && wcsstr(g_pt, L"LICENSES AND NOTICES") != NULL &&
                 wcsstr(g_pt, L"GNU GENERAL PUBLIC LICENSE") != NULL,
                 "Licenses opens the licences and notices (the GPL is in it)");
        nt_check(g_pl_n > 3000 && g_content_h > g_view_h * 20,
                 "... thousands of lines, scrolled by the same bar");
        on_key(VK_END);
        render();
        nt_check(g_scroll[scr_i()] == scroll_max() && g_scroll[PAGE_WELCOME] == 0, "... to the end and back");
        on_key(VK_ESCAPE);
        nt_check(g_pane == 0 && g_page == PAGE_WELCOME, "Esc returns to the first page");
        /* the question about closing the window, while she is reading: the question wins */
        fx_machine(0); fx_ready();
        g_page = PAGE_PROGRESS; g_install_running = 1; g_install_finished = 0;
        render();
        widget_activate(ID_LICENSES);
        render();
        nt_check(g_pane == 2, "on the install page too, Licenses opens");
        nt_check(!close_asked(NULL) && g_confirm == 1, "closing the window while reading asks");
        render();
        nt_check(!wcscmp(primary_label(), L"Keep going"), "... and the question is what she sees, not the text");
        on_key(VK_ESCAPE);
        nt_check(g_confirm == 0 && g_pane == 2, "Esc answers it, and she is still reading");
        on_key(VK_ESCAPE);
        nt_check(g_pane == 0, "and the next Esc closes the pane");
        g_install_running = 0;
        g_page = PAGE_WELCOME;
        /* a page that moves on closes the pane (the check can finish behind it) */
        widget_activate(ID_LICENSES);
        goto_page(PAGE_STYLE);
        nt_check(g_pane == 0, "a page that changes closes it");
        g_page = PAGE_WELCOME;
    }

    if (g_nt) fprintf(g_nt, "\n14. every letter on every page is in the face\n");
    {
        /* the pages at every size were drawn above; draw the ones that only
         * show with a particular finding, and the whole fix table */
        g_missing = 0;
        /* the first page while the download runs, both panes' headings, and the
         * Ready page with the agreement */
        fx_machine(0); fx_ready();
        g_page = PAGE_WELCOME; g_pane = 0; g_confirm = 0;
        InterlockedExchange(&g_dl_state, 1); render();
        InterlockedExchange(&g_dl_state, 2); render();
        InterlockedExchange(&g_dl_state, 0);
        g_page = PAGE_READY; render();
        g_pane = 1; render();
        g_pane = 2; render();
        g_pane = 0;
        memset(&g_report, 0, sizeof g_report);
        g_report.system_disk = -1;
        sbdb_judge(&g_report, 1, 0, (const unsigned char *)"", 0, -1, NULL, 0,
                   "Microsoft Corporation UEFI CA 2011\n");
        g_pf_valid = 1; g_page = PAGE_BLOCKED; g_sb_helper = 1;
        render();
        g_sb_helper = 0;
        for (int i = 0; fix_at(i); i++) {
            const fix_info *f = fix_at(i);
            fx_machine(0);
            fx_add(f->id, PF_BLOCK, "a synthetic problem");
            g_page = PAGE_BLOCKED; render();
            const wchar_t *x = fix_instead(f->id), *y = fix_note(f->id);
            if (x) glyph_audit(g_f_body, x, -1);
            if (y) glyph_audit(g_f_body, y, -1);
            if (f->after) glyph_audit(g_f_body, f->after, -1);
        }
        char what[96];
        snprintf(what, sizeof what, "every letter drawn is in the face (first missing: U+%04X)",
                 g_missing_cp);
        nt_check(g_missing == 0 || !g_face_ok_early, what);
        nt_check(g_face_ok_early, "the face loaded from inside the .exe (else Segoe UI is drawing)");
    }

    if (g_nt) {
        fprintf(g_nt, "\n%s (%d failures)\n", g_nt_fail ? "FAILED" : "ALL PASS", g_nt_fail);
        fclose(g_nt);
    }
    return g_nt_fail ? 1 : 0;
}

/* ═══════════════════════════════════════════════════════════════════ */
int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    plat_harden_dll_search();                       /* SEC-003: before anything is loaded */
    (void)prev; (void)cmd;

    /* Dev harness switches. They only render and report; none of them can
     * change anything on the machine. */
    const char *shot = NULL, *navtest = NULL, *report_dir = NULL;
    int resume = 0, report = 0;
    for (int i = 1; i < __argc; i++) {
        if (!strcmp(__argv[i], "--resume")) resume = 1;
        /* `AurOS-Setup.exe --report [FOLDER]`: write what is inside this
         * PC and stop (src/aurbridge/hwinv.h). Read-only. */
        else if (!strcmp(__argv[i], "--report")) {
            report = 1;
            if (i + 1 < __argc && __argv[i + 1][0] != '-') report_dir = __argv[++i];
        }
        else if (!strcmp(__argv[i], "--shot") && i + 1 < __argc) shot = __argv[++i];
        else if (!strcmp(__argv[i], "--navtest") && i + 1 < __argc) navtest = __argv[++i];
        else if (!strcmp(__argv[i], "--selftest") && i + 1 < __argc) {
            g_selftest = 1;
            _snprintf(g_selftest_dir, sizeof g_selftest_dir - 1, "%s", __argv[++i]);
            g_selftest_dir[sizeof g_selftest_dir - 1] = 0;
        }
    }

    if (report) return hwinv_report_main(report_dir, report_dir == NULL, NULL);
    dpi_opt_in();
    fonts_load();
    if (shot) return shot_run(shot);
    if (navtest) return nav_test(navtest);

    /* ONE INSTALLER AT A TIME. After a restart the installer opens itself
     * (fix.h); somebody who also double-clicks it must not get two
     * installers working on one disk. The second one says so and goes. */
    HANDLE one = CreateMutexW(NULL, TRUE, L"Local\\AurOSInstaller");
    if (one && GetLastError() == ERROR_ALREADY_EXISTS && !g_selftest) {
        MessageBoxW(NULL, L"AurOS Setup is already open.", L"AurOS Setup",
                    MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    /* The task that opened this after a restart has done its job; take it
     * away first, whether this start came from it or from a double-click. */
    if (!g_selftest) fix_resume_done();

    detect_defaults();
    /* COM on this thread, for the screen-reader object (WM_GETOBJECT). */
    CoInitialize(NULL);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize        = sizeof wc;
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"AurBridgeWizard";
    /* The installer's own icon (resource 1, build/aurbridge), and
     * Windows' plain one in a build that has none. */
    wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wc.hIconSm       = wc.hIcon;
    if (!RegisterClassExW(&wc)) return 1;

    g_dpi = dpi_for(NULL);
    RECT r = { 0, 0, S(1120), S(760) };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    /* Fit the screen, taskbar excluded. Taller than the screen, the page
     * had nothing to scroll and its buttons could not be pressed. */
    RECT wa;
    if (!work_area(NULL, &wa)) SetRect(&wa, 0, 0, GetSystemMetrics(SM_CXSCREEN),
                                       GetSystemMetrics(SM_CYSCREEN));
    if (ww > wa.right - wa.left) ww = wa.right - wa.left;
    if (wh > wa.bottom - wa.top) wh = wa.bottom - wa.top;
    int sx = wa.left + ((wa.right - wa.left) - ww) / 2;
    int sy = wa.top + ((wa.bottom - wa.top) - wh) / 2;

    HWND h = CreateWindowExW(0, wc.lpszClassName, L"AurOS Setup",
                             WS_OVERLAPPEDWINDOW, sx, sy, ww, wh,
                             NULL, NULL, inst, NULL);
    if (!h) return 1;

    /* told when she unlocks the computer (WM_WTSSESSION_CHANGE); loaded
     * by name, as Windows Server's runners may lack the session service */
    {
        HMODULE wts = (HMODULE)plat_load_system_dll("wtsapi32.dll");   /* SEC-003 */
        typedef BOOL (WINAPI *PFN_WRSN)(HWND, DWORD);
        PFN_WRSN reg = wts ? (PFN_WRSN)(void *)GetProcAddress(wts, "WTSRegisterSessionNotification")
                           : NULL;
        if (reg) reg(h, 0 /* NOTIFY_FOR_THIS_SESSION */);
    }
    ShowWindow(h, g_selftest ? SW_SHOWNOACTIVATE : show);
    UpdateWindow(h);
    /* THE CHECK STARTS BY ITSELF, on the first page, while she reads.
     * Opened again after a restart it asked for, it is where it left off. */
    if (resume && !g_selftest) {
        g_resumed = 1;
        SetForegroundWindow(h);
    }
    start_check(PAGE_STYLE);

    MSG msg;
    if (g_selftest) {
        int quit = 0;
        while (!quit) {
            for (int drained = 0; drained < 8; drained++) {
                if (!PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) break;
                if (msg.message == WM_QUIT) { quit = 1; break; }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (quit) break;
            tick();
            Sleep(10);
        }
        fonts_free();
        return 0;
    }
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    fonts_free();
    return 0;
}
