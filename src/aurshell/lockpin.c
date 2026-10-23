/* lockpin.c — the page that asks for her PIN when AurOS starts
 *
 * docs/security/THEFT.md, sections 3 and 6. AurOS's drive is encrypted;
 * the key is sealed inside the computer's security chip (TPM 2.0) and
 * the chip only hands it over for the right PIN. This is the page she
 * types that PIN into. It runs in the initramfs, before anything of
 * AurOS has started, so it is built like aurscreen: it paints straight
 * to the display (src/aurshell/kms.c), reads keys straight from the
 * kernel (evdev: the physical key, so the number keys work on every
 * keyboard layout and with Num Lock off), and talks to the disk through
 * libcryptsetup, opened with dlopen so that nothing needs its headers
 * to build this.
 *
 * WHAT IT DECIDES, AND HOW IT KNOWS
 *
 * systemd 255's TPM token answers -131 (ENOTRECOVERABLE) to a wrong
 * PIN, to a chip that is refusing every PIN because of too many wrong
 * ones, AND to a chip that is not the one the key was sealed in --
 * measured, tools/theftunit.sh. Those need three different sentences,
 * and one of them ("That PIN didn't work") would be a lie in the other
 * two cases: she would type her right PIN again and again. So the chip
 * itself is asked, before and after each try, for its own count of
 * wrong PINs (TPM2_PT_LOCKOUT_COUNTER) and whether it is locked out:
 *
 *   the count went up                  -> that PIN was wrong
 *   the chip says it is locked out     -> too many tries; how long to wait
 *                                         is the chip's own number
 *   neither, or there is no chip       -> this computer has changed
 *                                         (a cleared or different chip,
 *                                         or a PCR the key is bound to):
 *                                         the rescue code, this once
 *
 * The rescue code is 24 numbers, typed on the same number pad. After it
 * opens the drive she chooses her PIN again (it may be the same one),
 * and that PIN goes to /run/auros-lock/repin for `lockctl repin` in the
 * started system, which seals a new key in the chip it finds there.
 * The same page is therefore the way back from a forgotten PIN, a
 * cleared chip and a firmware change: one path, tested once.
 *
 * Before she has chosen a PIN at all, the drive opens with a temporary
 * key everybody knows (THEFT.md section 1 says why that is honest), and
 * this program opens it without painting anything.
 *
 *   aurlock-pin DEVICE NAME        open DEVICE as /dev/mapper/NAME
 *   aurlock-pin --try DEVICE NAME  the same decisions, answers as lines on
 *                                  stdin: "1234" or "rescue 1234...":
 *                                  prints one verdict per line (tests)
 *   aurlock-pin --png F --size W H --page P [--theme F] [--typed N]
 *                                  paint one page to a PNG (screenshots)
 *   aurlock-pin --tpm-status       the chip's numbers, key=value
 *
 * AURLOCK_TPM2_DEVICE (tests only) names a TCTI such as
 * "swtpm:path=/run/swtpm.sock" instead of the computer's own chip.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "kms.h"
#include "draw.h"
#include "../common/font.h"
#include "../common/png.h"
#include "../common/theme.h"

/* The temporary key the drive opens with until she chooses a PIN.
 * rootfs/usr/lib/auros/lockctl uses the same bytes. */
#define TEMP_KEY   "AurOS: no PIN has been chosen yet"
#define RESCUE_LEN 24          /* six groups of four numbers           */
#define PIN_MIN    4
#define PIN_MIN_NOTPM 6        /* nothing counts the tries without a chip */
#define PIN_MAX    6
#define REPIN_DIR  "/run/auros-lock"

/* ── libcryptsetup, by dlopen ─────────────────────────────────────── */

struct crypt_device;
static struct {
    int  (*init)(struct crypt_device **, const char *);
    int  (*load)(struct crypt_device *, const char *, void *);
    void (*free_)(struct crypt_device *);
    int  (*by_pass)(struct crypt_device *, const char *, int, const char *, size_t, uint32_t);
    int  (*by_token_pin)(struct crypt_device *, const char *, const char *, int,
                         const char *, size_t, void *, uint32_t);
    int  (*token_status)(struct crypt_device *, int, const char **);
    int  (*token_json)(struct crypt_device *, int, const char **);
    void (*set_log)(struct crypt_device *, void (*)(int, const char *, void *), void *);
    /* keyslot contexts (libcryptsetup 2.6+): a key sealed in the chip
     * authorising a new passphrase, for lockctl's change-pin */
    int  (*kc_pass)(struct crypt_device *, const char *, size_t, void **);
    int  (*kc_token)(struct crypt_device *, int, const char *, const char *, size_t, void *, void **);
    int  (*kc_add)(struct crypt_device *, int, void *, int, void *, uint32_t);
    void (*kc_free)(void *);
} cs;
#define CRYPT_ANY_SLOT  -1
#define CRYPT_ANY_TOKEN -1
#define CRYPT_ACTIVATE_ALLOW_DISCARDS (1 << 3)

static void quiet_log(int lvl, const char *m, void *u) { (void)lvl; (void)m; (void)u; }

static int cs_load(void)
{
    void *h = dlopen("libcryptsetup.so.12", RTLD_NOW);
    if (!h) return -1;
    *(void **)&cs.init         = dlsym(h, "crypt_init");
    *(void **)&cs.load         = dlsym(h, "crypt_load");
    *(void **)&cs.free_        = dlsym(h, "crypt_free");
    *(void **)&cs.by_pass      = dlsym(h, "crypt_activate_by_passphrase");
    *(void **)&cs.by_token_pin = dlsym(h, "crypt_activate_by_token_pin");
    *(void **)&cs.token_status = dlsym(h, "crypt_token_status");
    *(void **)&cs.token_json   = dlsym(h, "crypt_token_json_get");
    *(void **)&cs.set_log      = dlsym(h, "crypt_set_log_callback");
    *(void **)&cs.kc_pass      = dlsym(h, "crypt_keyslot_context_init_by_passphrase");
    *(void **)&cs.kc_token     = dlsym(h, "crypt_keyslot_context_init_by_token");
    *(void **)&cs.kc_add       = dlsym(h, "crypt_keyslot_add_by_keyslot_context");
    *(void **)&cs.kc_free      = dlsym(h, "crypt_keyslot_context_free");
    return cs.init && cs.load && cs.free_ && cs.by_pass && cs.by_token_pin &&
           cs.token_status && cs.token_json ? 0 : -1;
}

/* The tests' chip, or NULL (and "" is NULL: lockctl passes the variable
 * through, empty, on her computer). */
static const char *test_tpm(void)
{
    const char *v = getenv("AURLOCK_TPM2_DEVICE");
    return v && *v ? v : NULL;
}

/* ── the chip's own count of wrong PINs (tss2-esys, by dlopen) ────── */

typedef struct {
    int      ok;               /* we could ask a chip at all            */
    unsigned counter, max_tries, interval, recovery;
    int      in_lockout, lockout_auth_set;
} tpm_da;

static int tpm_da_read(tpm_da *t)
{
    static void *tl, *es;
    static int (*ldr_init)(const char *, void **);
    static void (*ldr_fin)(void **);
    static int (*esys_init)(void **, void *, void *);
    static void (*esys_fin)(void **);
    static int (*getcap)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                         uint32_t, uint8_t *, void **);
    static void (*esys_free)(void *);
    memset(t, 0, sizeof *t);
    if (!tl) {
        setenv("TSS2_LOG", "all+none", 0);       /* its errors are ours to word */
        tl = dlopen("libtss2-tctildr.so.0", RTLD_NOW);
        es = dlopen("libtss2-esys.so.0", RTLD_NOW);
        if (!tl || !es) return -1;
        *(void **)&ldr_init  = dlsym(tl, "Tss2_TctiLdr_Initialize");
        *(void **)&ldr_fin   = dlsym(tl, "Tss2_TctiLdr_Finalize");
        *(void **)&esys_init = dlsym(es, "Esys_Initialize");
        *(void **)&esys_fin  = dlsym(es, "Esys_Finalize");
        *(void **)&getcap    = dlsym(es, "Esys_GetCapability");
        *(void **)&esys_free = dlsym(es, "Esys_Free");
    }
    if (!ldr_init || !ldr_fin || !esys_init || !esys_fin || !getcap || !esys_free) return -1;

    const char *dev = test_tpm();
    if (!dev) {
        /* Only the kernel's resource manager: never the raw /dev/tpm0,
         * which a second user of the chip would trip over. */
        if (access("/dev/tpmrm0", R_OK | W_OK) != 0) return -1;
        dev = "device:/dev/tpmrm0";
    }
    void *tcti = NULL, *ctx = NULL;
    if (ldr_init(dev, &tcti) != 0) return -1;
    if (esys_init(&ctx, tcti, NULL) != 0) { ldr_fin(&tcti); return -1; }

    /* TPMS_CAPABILITY_DATA for TPM2_CAP_TPM_PROPERTIES is a uint32
     * capability, a uint32 count, then {property, value} uint32 pairs:
     * no padding anywhere, so it is read as an array of uint32. */
    uint8_t more = 0;
    uint32_t *cap = NULL;
    int rc = getcap(ctx, 0xfff, 0xfff, 0xfff, 6 /* TPM_PROPERTIES */,
                    0x200 /* TPM2_PT_PERMANENT, the first PT_VAR */, 32, &more, (void **)&cap);
    if (rc == 0 && cap) {
        uint32_t n = cap[1];
        for (uint32_t i = 0; i < n && i < 64; i++) {
            uint32_t p = cap[2 + 2 * i], v = cap[3 + 2 * i];
            if (p == 0x200) { t->lockout_auth_set = !!(v & (1u << 2));
                              t->in_lockout       = !!(v & (1u << 9)); }
            else if (p == 0x20E) t->counter   = v;
            else if (p == 0x20F) t->max_tries = v;
            else if (p == 0x210) t->interval  = v;
            else if (p == 0x211) t->recovery  = v;
        }
        t->ok = 1;
        esys_free(cap);
    }
    esys_fin(&ctx);
    ldr_fin(&tcti);
    return t->ok ? 0 : -1;
}

/* ── the drive ────────────────────────────────────────────────────── */

typedef enum { V_OPEN, V_WRONG, V_LOCKED, V_CHANGED, V_ERROR } verdict;
static const char *VNAME[] = { "open", "wrong", "locked", "changed", "error" };

typedef struct {
    const char *dev, *name;
    struct crypt_device *cd;
    int has_tpm_token;         /* a key sealed in a chip                */
    int pin_len;               /* from the auros-hint token; 0 = unknown */
} drive;

static int drive_load(drive *d)
{
    if (cs_load() < 0) { fprintf(stderr, "aurlock-pin: no libcryptsetup\n"); return -1; }
    if (cs.init(&d->cd, d->dev) < 0 || cs.load(d->cd, "LUKS2", NULL) < 0) {
        fprintf(stderr, "aurlock-pin: %s is not an encrypted AurOS drive\n", d->dev);
        return -1;
    }
    if (cs.set_log) cs.set_log(d->cd, quiet_log, NULL);
    for (int i = 0; i < 32; i++) {
        const char *type = NULL;
        int st = cs.token_status(d->cd, i, &type);
        if (st <= 1 || !type) continue;          /* invalid or inactive */
        if (!strcmp(type, "systemd-tpm2")) d->has_tpm_token = 1;
        if (!strcmp(type, "auros-hint")) {
            const char *j = NULL;
            if (cs.token_json(d->cd, i, &j) >= 0 && j) {
                const char *p = strstr(j, "\"pin_length\"");
                if (p && (p = strchr(p, ':'))) {
                    int n = atoi(p + 1);
                    if (n >= PIN_MIN && n <= PIN_MAX) d->pin_len = n;
                }
            }
        }
    }
    return 0;
}

static uint32_t act_flags(void) { return CRYPT_ACTIVATE_ALLOW_DISCARDS; }

static int try_temp(drive *d)
{
    return cs.by_pass(d->cd, d->name, CRYPT_ANY_SLOT, TEMP_KEY, strlen(TEMP_KEY), act_flags()) >= 0;
}

/* systemd's plugin takes the chip from its own parameters, never from
 * the environment: the tests' chip has to be passed in. The layout is
 * systemd 255's systemd_tpm2_plugin_params; it is used only when
 * AURLOCK_TPM2_DEVICE is set, which nothing on her computer sets. */
typedef struct { uint32_t search_pcr_mask; const char *device, *sig, *pcrlock; } tpm2_params;


#include <stdarg.h>
/* Diagnostics: stderr, and with AURLOCK_CONSOLE=1 (the initramfs sets
 * it) /dev/console too, because there stderr goes to a log file and the
 * console is where a person, or the VM test's serial line, looks. */
static void diag(const char *fmt, ...)
{
    char b[300];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof b) n = (int)sizeof b - 1;
    if (write(2, b, (size_t)n) < 0) { /* nothing to be done */ }
    const char *c = getenv("AURLOCK_CONSOLE");
    if (c && *c == '1') {
        int fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_CLOEXEC);
        if (fd >= 0) { if (write(fd, b, (size_t)n) < 0) { /* ignored */ } close(fd); }
    }
}

static long ms_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

/* One line per try on the console, for the tests and for a technician:
 * the verdict and how long the chip (or argon2) took. Never the PIN. */
static verdict said(verdict v, const char *what, long t0)
{
    diag("aurlock-pin: %s %s in %ld ms\n", what, v == 0 ? "open" : v == 1 ? "wrong" :
            v == 2 ? "locked" : v == 3 ? "changed" : "error", ms_now() - t0);
    return v;
}

static verdict try_pin_(drive *d, const char *pin);
static verdict try_pin(drive *d, const char *pin)
{
    long t0 = ms_now();
    return said(try_pin_(d, pin), "PIN", t0);
}

static verdict try_pin_(drive *d, const char *pin)
{
    size_t n = strlen(pin);
    if (!d->has_tpm_token) {
        int r = cs.by_pass(d->cd, d->name, CRYPT_ANY_SLOT, pin, n, act_flags());
        return r >= 0 ? V_OPEN : r == -EPERM ? V_WRONG : V_ERROR;
    }
    tpm_da before, after;
    if (tpm_da_read(&before) < 0) return V_CHANGED;      /* no chip answering */
    if (before.in_lockout) return V_LOCKED;
    tpm2_params p = { UINT32_MAX, test_tpm(), NULL, NULL };
    int r = cs.by_token_pin(d->cd, d->name, "systemd-tpm2", CRYPT_ANY_TOKEN, pin, n,
                            p.device ? &p : NULL, act_flags());
    if (r >= 0) return V_OPEN;
    if (tpm_da_read(&after) < 0) return V_CHANGED;
    if (after.in_lockout) return V_LOCKED;
    /* ponytail: a recovery tick landing between the two reads would make
     * a wrong PIN look like a changed computer -- one interval in 600 s
     * against a read pair of ~0.1 s, and the rescue page has a way back. */
    if (after.counter > before.counter) return V_WRONG;
    return V_CHANGED;
}

static verdict try_rescue(drive *d, const char *code)
{
    long t0 = ms_now();
    int r = cs.by_pass(d->cd, d->name, CRYPT_ANY_SLOT, code, strlen(code), act_flags());
    return said(r >= 0 ? V_OPEN : r == -EPERM ? V_WRONG : V_ERROR, "rescue code", t0);
}

/* After the rescue code: the PIN she chose, for `lockctl repin` in the
 * started system. /run is RAM and is carried into the system. The code
 * goes with it because sealing a new key needs one that opens the drive;
 * lockctl deletes the file the moment it has read it. */
static int write_repin(const char *pin, const char *code)
{
    mkdir(REPIN_DIR, 0700);
    int fd = open(REPIN_DIR "/repin", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    char b[128];
    int n = snprintf(b, sizeof b, "pin=%s\nrescue=%s\n", pin, code);
    int ok = write(fd, b, (size_t)n) == n;
    fsync(fd);
    close(fd);
    explicit_bzero(b, sizeof b);
    return ok ? 0 : -1;
}

/* ── what is on the screen ────────────────────────────────────────── */

enum { P_PIN, P_LOCKED, P_RESCUE, P_NEWPIN, P_NEWPIN2, P_BUSY, P_DONE };
enum { M_NONE, M_WRONG, M_RESCUE_WRONG, M_DIFFERENT, M_TRY_AGAIN_NOW, M_TOO_SHORT, M_ERROR };

typedef struct {
    int  page, msg;
    int  changed;              /* rescue because the computer changed   */
    int  has_tpm;              /* a chip is answering now               */
    int  pin_len;              /* known length: submit without OK       */
    char typed[32];
    int  ntyped;
    char code[RESCUE_LEN + 1]; /* the rescue code that opened the drive */
    char first[PIN_MAX + 1];   /* the new PIN, first time               */
    unsigned wait_min;         /* locked out: the chip's own interval   */
    int  pressed;              /* key shown pressed, -1 none            */
    int  cx, cy, show_cursor;
} ui;

typedef struct {
    uint32_t bg, ink, body, dim, faint, accent, on_accent, bad, key, line;
    font *h1, *txt, *small, *digit;
    float k;
} look;

static font *pick(const char *want, float px, int bold)
{
    char p[256];
    if (want && *want) {
        snprintf(p, sizeof p, "/usr/share/auros/fonts/%s.ttf", want);
        font *f = font_load(p, px);
        if (f) return f;
    }
    static const char *R[] = { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", NULL };
    static const char *B[] = { "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                               "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", NULL };
    const char **t = bold ? B : R;
    for (int i = 0; t[i]; i++) { font *f = font_load(t[i], px); if (f) return f; }
    return NULL;
}

/* Her look, when the hook copied it into the initramfs; the staging
 * screen's palette (src/aurscreen) when it did not. */
static void look_load(look *L, const char *theme_path, int h)
{
    static theme_t t;
    memset(&t, 0, sizeof t);
    theme_load(&t, theme_path ? theme_path : "/etc/auros/shell.conf");
    L->bg     = theme_color(&t, "bg",      0x0B0E14);
    L->ink    = theme_color(&t, "fg_hi",   0xECEFF2);
    L->body   = theme_color(&t, "fg",      0xD6DAE0);
    L->dim    = theme_color(&t, "subtle",  0x8C95A0);
    L->faint  = theme_color(&t, "overlay", 0x4A525C);
    L->accent = theme_color(&t, "accent",  0x7DD3C0);
    L->bad    = theme_color(&t, "err",     0xF2788D);
    L->key    = theme_color(&t, "surface", 0x161B23);
    L->line   = theme_color(&t, "overlay", 0x4A525C);
    L->on_accent = L->bg;
    float k = (float)h / 720.f;
    L->k = k < 0.85f ? 0.85f : k > 2.4f ? 2.4f : k;
    /* EASY.md: nothing she reads is under 18 px, at any size */
#define AT_LEAST(a, b) ((a) > (b) ? (a) : (b))
    L->h1    = pick(theme_str(&t, "font_display", NULL), AT_LEAST(34.f, 40.f * L->k), 1);
    L->txt   = pick(theme_str(&t, "font_text", NULL), AT_LEAST(20.f, 22.f * L->k), 0);
    L->small = pick(theme_str(&t, "font_text", NULL), AT_LEAST(18.f, 18.f * L->k), 0);
    L->digit = pick(theme_str(&t, "font_text_bold", theme_str(&t, "font_display", NULL)),
                    30.f * L->k, 1);
}

static const char *title_of(const ui *u)
{
    switch (u->page) {
    case P_LOCKED:  return "Too many tries";
    case P_RESCUE:  return u->changed ? "This computer has changed" : "Type your rescue code";
    case P_NEWPIN:  return "Choose your PIN";
    case P_NEWPIN2: return "Type your PIN once more";
    case P_DONE:    return "Starting AurOS";
    default:        return "Type your PIN to start AurOS";
    }
}

static void say_of(const ui *u, char *b, size_t n, int *bad)
{
    *bad = 0;
    switch (u->msg) {
    case M_WRONG:        *bad = 1; snprintf(b, n, "That PIN didn't work. Try again."); return;
    case M_RESCUE_WRONG: *bad = 1; snprintf(b, n, "That rescue code didn't work. Check the numbers against your paper and try again."); return;
    case M_DIFFERENT:    *bad = 1; snprintf(b, n, "Those two were not the same. Choose your PIN again."); return;
    case M_TOO_SHORT:    *bad = 1; snprintf(b, n, "Your PIN needs %d to %d numbers.", u->has_tpm ? PIN_MIN : PIN_MIN_NOTPM, PIN_MAX); return;
    case M_ERROR:        *bad = 1; snprintf(b, n, "Something went wrong opening the drive. Try again, or use your rescue code."); return;
    case M_TRY_AGAIN_NOW: snprintf(b, n, "You can try your PIN again now."); return;
    }
    switch (u->page) {
    case P_LOCKED:
        snprintf(b, n, "This computer is waiting before it lets anyone try again. "
                 "You can try your PIN again in about %u minute%s, or use your rescue code now.",
                 u->wait_min ? u->wait_min : 10, u->wait_min == 1 ? "" : "s");
        return;
    case P_RESCUE:
        snprintf(b, n, u->changed
            ? "Since you last used it, something about this computer changed, so it needs your rescue code this once. It is on the paper you kept: six groups of four numbers."
            : "It is on the paper you kept when you chose your PIN: six groups of four numbers.");
        return;
    case P_NEWPIN:
        snprintf(b, n, u->has_tpm
            ? "Pick 4 to 6 numbers you will remember. It can be the same PIN as before."
            : "This computer has no security chip, so pick 6 numbers you will remember. It can be the same PIN as before.");
        return;
    case P_NEWPIN2: snprintf(b, n, "So we know it is the one you meant."); return;
    case P_BUSY:    snprintf(b, n, "One moment."); return;
    default:        b[0] = 0; return;
    }
}

/* ── geometry: one function, used by painting and by the pointer ──── */

enum { K_DEL = 10, K_OK = 11, NKEY = 12 };
typedef struct {
    int  L, top, colw;
    int  title_y, say_y, entry_y, link_y;
    rect key[NKEY];
    rect link;
    const char *link_text;
} geo;

static const char *link_of(const ui *u)
{
    switch (u->page) {
    case P_PIN:    return "I forgot my PIN";
    case P_LOCKED: return "Use my rescue code";
    case P_RESCUE: return u->changed ? NULL : "Back to my PIN";
    default:       return NULL;
    }
}

static int keys_shown(const ui *u)
{
    return u->page == P_PIN || u->page == P_RESCUE || u->page == P_NEWPIN || u->page == P_NEWPIN2;
}

/* The pad: 1 2 3 / 4 5 6 / 7 8 9 / Delete 0 OK, on the right of a wide
 * screen, under the words on a tall one. Keys are 84 px at 720 lines:
 * 71 px on a 1024x600 panel, well over EASY.md's 44. */
static void layout(const ui *u, const look *L, int w, int h, geo *g)
{
    memset(g, 0, sizeof *g);
    float k = L->k;
    int kw = (int)(84 * k), gap = (int)(14 * k);
    int padw = 3 * kw + 2 * gap, padh = 4 * kw + 3 * gap;
    int side = w >= (int)(h * 1.2f);
    g->L = (int)(w * 0.09f);
    g->top = (int)(h * 0.11f);
    int px, py;
    if (side) {
        px = w - g->L - padw;
        py = (h - padh) / 2 + (int)(24 * k);
        g->colw = px - g->L - (int)(56 * k);
    } else {
        px = (w - padw) / 2;
        py = h - padh - (int)(40 * k);
        g->colw = w - 2 * g->L;
    }
    static const int order[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, K_DEL, 0, K_OK };
    for (int i = 0; i < 12; i++)
        g->key[order[i]] = (rect){ px + (i % 3) * (kw + gap), py + (i / 3) * (kw + gap), kw, kw };
    if (!keys_shown(u))
        for (int i = 0; i < NKEY; i++) g->key[i].w = 0;
    g->title_y = g->top + (int)(56 * k);
    g->link_text = link_of(u);
}

/* Words broken onto lines no wider than `w`. Returns the height used. */
static int wrap(surface *s, font *f, int x, int y, int w, const char *t, uint32_t c, int paint)
{
    if (!f || !t || !*t) return 0;
    int lh = (int)(font_line_height(f) * 1.3f), used = 0;
    char line[640] = "", word[128];
    const char *p = t;
    while (*p) {
        while (*p == ' ') p++;
        size_t n = strcspn(p, " ");
        if (n >= sizeof word) n = sizeof word - 1;
        memcpy(word, p, n); word[n] = 0;
        p += n;
        char next[sizeof line + sizeof word + 2];
        snprintf(next, sizeof next, "%s%s%s", line, *line ? " " : "", word);
        if (*line && font_text_width(f, next) > (float)w) {
            if (paint && s) font_draw(f, s->px, s->w, s->h, s->stride, (float)x,
                                      (float)(y + used) + font_ascent(f), line, c, 1.f);
            used += lh;
            snprintf(line, sizeof line, "%s", word);
        } else {
            snprintf(line, sizeof line, "%.*s", (int)sizeof line - 1, next);
        }
    }
    if (*line) {
        if (paint && s) font_draw(f, s->px, s->w, s->h, s->stride, (float)x,
                                  (float)(y + used) + font_ascent(f), line, c, 1.f);
        used += lh;
    }
    return used;
}

static void centred(surface *s, font *f, rect r, const char *t, uint32_t c)
{
    if (!f) return;
    float tw = font_text_width(f, t);
    float y = (float)r.y + ((float)r.h - font_line_height(f)) / 2.f + font_ascent(f);
    font_draw(f, s->px, s->w, s->h, s->stride, (float)r.x + ((float)r.w - tw) / 2.f, y, t, c, 1.f);
}

static int ok_enabled(const ui *u)
{
    if (u->page == P_RESCUE) return u->ntyped == RESCUE_LEN;
    /* never spend one of the chip's counted tries on a PIN that is too
     * short to be hers */
    int min = u->page == P_PIN ? (u->pin_len ? u->pin_len : PIN_MIN)
            : u->has_tpm ? PIN_MIN : PIN_MIN_NOTPM;
    return u->ntyped >= min;
}

/* Paints, and fills in where the one text button ended up (it moves
 * with the words above it, so painting is what knows). */
static void paint(surface *s, const ui *u, const look *L, geo *g)
{
    layout(u, L, s->w, s->h, g);
    float k = L->k;
    surface_fill(s, 0xFF000000u | L->bg);

    /* the mark: the same diamond as the website, the boot menu and the
     * staging screen */
    float cx = (float)g->L + 14.f * k, cy = (float)g->top + 14.f * k, r = 13.f * k;
    for (int dy = -(int)r; dy <= (int)r; dy++) {
        int half = (int)(r - (dy < 0 ? -dy : dy));
        draw_rect(s, (rect){ (int)cx - half, (int)cy + dy, 2 * half + 1, 1 }, L->accent, 1.f);
    }
    if (L->small)
        font_draw(L->small, s->px, s->w, s->h, s->stride, (float)g->L + 40.f * k,
                  (float)g->top + 5.f * k + font_ascent(L->small), "AurOS", L->dim, 1.f);

    int y = g->title_y;
    y += wrap(s, L->h1, g->L, y, g->colw, title_of(u), L->ink, 1) + (int)(10 * k);

    char say[512]; int bad;
    say_of(u, say, sizeof say, &bad);
    y += wrap(s, L->txt, g->L, y, g->colw, say, bad ? L->bad : L->body, 1);
    y += (int)(30 * k);

    /* what she has typed: dots for a PIN, the numbers themselves for the
     * rescue code, so she can check them against her paper */
    if (u->page == P_RESCUE) {
        float dw = L->digit ? font_text_width(L->digit, "0") : 18.f;
        int gw = (int)(4 * dw + 28 * k), gh = (int)(56 * k), gg = (int)(14 * k);
        for (int gi = 0; gi < 6; gi++) {
            rect b = { g->L + (gi % 3) * (gw + gg), y + (gi / 3) * (gh + gg), gw, gh };
            int filled = u->ntyped - gi * 4;
            int cur = filled >= 0 && filled < 4;
            draw_round_rect(s, b, corners_all(8.f * k), L->key, 1.f);
            draw_round_rect_border(s, b, corners_all(8.f * k), cur ? 2.f * k : 1.f,
                                   cur ? L->accent : L->line, 1.f);
            for (int j = 0; j < 4; j++) {
                int at = gi * 4 + j;
                char c[2] = { at < u->ntyped ? u->typed[at] : 0, 0 };
                float x = (float)b.x + 14.f * k + j * dw;
                if (c[0] && L->digit)
                    font_draw(L->digit, s->px, s->w, s->h, s->stride, x,
                              (float)b.y + ((float)gh - font_line_height(L->digit)) / 2.f
                                  + font_ascent(L->digit), c, L->ink, 1.f);
                else
                    draw_circle(s, x + dw / 2.f, (float)b.y + gh / 2.f, 3.f * k, L->faint, 1.f);
            }
        }
        y += 2 * gh + gg + (int)(26 * k);
    } else if (u->page == P_PIN || u->page == P_NEWPIN || u->page == P_NEWPIN2) {
        int slots = u->page == P_PIN && u->pin_len ? u->pin_len : PIN_MAX;
        float rr = 9.f * k, step = 34.f * k;
        for (int i = 0; i < slots; i++) {
            float x = (float)g->L + rr + i * step, yy = (float)y + rr;
            if (i < u->ntyped) draw_circle(s, x, yy, rr, L->ink, 1.f);
            else {
                draw_circle(s, x, yy, rr, L->faint, 1.f);
                draw_circle(s, x, yy, rr - 2.f * k, L->bg, 1.f);
            }
        }
        y += (int)(2 * rr) + (int)(34 * k);
    } else {
        y += (int)(10 * k);
    }

    if (g->link_text && L->txt) {
        int tw = (int)font_text_width(L->txt, g->link_text);
        int lh = (int)font_line_height(L->txt);
        int hh = lh + (int)(24 * k) > 48 ? lh + (int)(24 * k) : 48;   /* EASY.md: >= 44 */
        g->link = (rect){ g->L - (int)(12 * k), y, tw + (int)(24 * k), hh };
        if (u->page == P_LOCKED) {
            draw_round_rect(s, g->link, corners_all(8.f * k), L->accent, 1.f);
            centred(s, L->txt, g->link, g->link_text, L->on_accent);
        } else {
            font_draw(L->txt, s->px, s->w, s->h, s->stride, (float)g->L,
                      (float)g->link.y + ((float)hh - lh) / 2.f + font_ascent(L->txt),
                      g->link_text, L->accent, 1.f);
            draw_hrule(s, g->L, g->link.y + (hh + lh) / 2 + (int)(3 * k), tw, 1, L->accent, 0.6f);
        }
    }

    for (int i = 0; i < NKEY; i++) {
        rect b = g->key[i];
        if (!b.w) continue;
        int okk = i == K_OK, en = okk ? ok_enabled(u) : i == K_DEL ? u->ntyped > 0 : 1;
        int down = u->pressed == i;
        uint32_t fill = okk && en ? L->accent : L->key;
        draw_round_rect(s, b, corners_all(12.f * k), fill, 1.f);
        draw_round_rect_border(s, b, corners_all(12.f * k), down ? 2.f * k : 1.f,
                               down ? L->accent : L->line, 1.f);
        if (i < 10) { char t[2] = { (char)('0' + i), 0 }; centred(s, L->digit, b, t, L->ink); }
        else centred(s, L->small, b, okk ? "OK" : "Delete",
                     okk && en ? L->on_accent : en ? L->body : L->faint);
    }

    if (u->show_cursor) {
        draw_circle(s, (float)u->cx, (float)u->cy, 9.f * k, L->bg, 1.f);
        draw_circle(s, (float)u->cx, (float)u->cy, 7.f * k, L->ink, 1.f);
    }
}

/* ── what a key press does ────────────────────────────────────────── */

typedef struct { drive *d; ui *u; int opened; } app;

static void clear_typed(ui *u) { explicit_bzero(u->typed, sizeof u->typed); u->ntyped = 0; }

static void go(ui *u, int page, int msg)
{
    u->page = page; u->msg = msg; clear_typed(u);
}

static void after_open(app *a);

/* `busy` paints "One moment" before the slow part (the chip takes up to
 * a second and a half, the no-chip PIN two seconds by design). */
static void submit(app *a, void (*busy)(app *))
{
    ui *u = a->u;
    char t[32];
    snprintf(t, sizeof t, "%.*s", u->ntyped, u->typed);
    int was = u->page;
    if (was == P_PIN || was == P_RESCUE) {
        u->page = P_BUSY; u->msg = M_NONE;
        if (busy) busy(a);
    }
    if (was == P_PIN) {
        verdict v = try_pin(a->d, t);
        if (v == V_OPEN) { a->opened = 1; go(u, P_DONE, M_NONE); }
        else if (v == V_WRONG) go(u, P_PIN, M_WRONG);
        else if (v == V_LOCKED) {
            tpm_da da; tpm_da_read(&da);
            u->wait_min = da.interval ? (da.interval + 59) / 60 : 10;
            go(u, P_LOCKED, M_NONE);
        } else if (v == V_CHANGED) { u->changed = 1; go(u, P_RESCUE, M_NONE); }
        else go(u, P_PIN, M_ERROR);
    } else if (was == P_RESCUE) {
        verdict v = try_rescue(a->d, t);
        if (v == V_OPEN) {
            snprintf(u->code, sizeof u->code, "%.*s", RESCUE_LEN, t);
            tpm_da da;
            u->has_tpm = tpm_da_read(&da) == 0;
            go(u, P_NEWPIN, M_NONE);
        } else go(u, P_RESCUE, v == V_WRONG ? M_RESCUE_WRONG : M_ERROR);
    } else if (was == P_NEWPIN) {
        int min = u->has_tpm ? PIN_MIN : PIN_MIN_NOTPM;
        if (u->ntyped < min) { u->msg = M_TOO_SHORT; return; }
        snprintf(u->first, sizeof u->first, "%.*s", PIN_MAX, t);
        go(u, P_NEWPIN2, M_NONE);
    } else if (was == P_NEWPIN2) {
        if (strcmp(t, u->first)) { explicit_bzero(u->first, sizeof u->first); go(u, P_NEWPIN, M_DIFFERENT); }
        else after_open(a);
    }
    explicit_bzero(t, sizeof t);
}

static void after_open(app *a)
{
    ui *u = a->u;
    /* The drive is already open (the rescue code opened it). If the PIN
     * cannot be handed over, the next start asks for the rescue code
     * again: worse, never a lock-out. */
    write_repin(u->first, u->code);
    explicit_bzero(u->first, sizeof u->first);
    explicit_bzero(u->code, sizeof u->code);
    a->opened = 1;
    go(u, P_DONE, M_NONE);
}

static void press(app *a, int key, void (*busy)(app *))
{
    ui *u = a->u;
    if (!keys_shown(u)) return;
    int max = u->page == P_RESCUE ? RESCUE_LEN : PIN_MAX;
    if (key >= 0 && key <= 9) {
        if (u->ntyped < max) {
            u->typed[u->ntyped++] = (char)('0' + key);
            if (u->msg != M_NONE && u->msg != M_TRY_AGAIN_NOW) u->msg = M_NONE;
        }
        /* a known length, and the rescue code's, need no OK */
        if ((u->page == P_PIN && u->pin_len && u->ntyped == u->pin_len) ||
            (u->page == P_RESCUE && u->ntyped == RESCUE_LEN))
            submit(a, busy);
    } else if (key == K_DEL) {
        if (u->ntyped) u->typed[--u->ntyped] = 0;
    } else if (key == K_OK) {
        if (ok_enabled(u) || u->page == P_NEWPIN) submit(a, busy);
        else if (u->page == P_PIN && u->ntyped) u->msg = M_TOO_SHORT;
    }
}

static void link_pressed(app *a)
{
    ui *u = a->u;
    if (u->page == P_PIN || u->page == P_LOCKED) { u->changed = 0; go(u, P_RESCUE, M_NONE); }
    else if (u->page == P_RESCUE && !u->changed) go(u, P_PIN, M_NONE);
}

/* ── tests: answers on stdin, verdicts on stdout ──────────────────── */

static int run_try(drive *d)
{
    if (try_temp(d)) { printf("open temp\n"); return 0; }
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        verdict v;
        if (!strncmp(line, "rescue ", 7)) v = try_rescue(d, line + 7);
        else v = try_pin(d, line);
        printf("%s\n", VNAME[v]);
        fflush(stdout);
        if (v == V_OPEN) return 0;
    }
    return 1;
}

/* ── no display: the console, in the same words ───────────────────── */

static int console_read(const char *prompt, char *out, size_t n)
{
    int fd = open("/dev/console", O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) return -1;
    struct termios t, o;
    int tty = tcgetattr(fd, &o) == 0;
    if (tty) { t = o; t.c_lflag &= ~(tcflag_t)ECHO; tcsetattr(fd, TCSANOW, &t); }
    dprintf(fd, "\n%s ", prompt);
    size_t i = 0; char c;
    while (i + 1 < n && read(fd, &c, 1) == 1 && c != '\n' && c != '\r') out[i++] = c;
    out[i] = 0;
    dprintf(fd, "\n");
    if (tty) tcsetattr(fd, TCSANOW, &o);
    close(fd);
    return 0;
}

static int run_console(drive *d)
{
    char b[64];
    for (;;) {
        if (console_read("Type your PIN to start AurOS, then press Enter:", b, sizeof b) < 0) {
            struct timespec ts = { 5, 0 }; nanosleep(&ts, NULL); continue;
        }
        verdict v = b[0] == 'r' ? try_rescue(d, b + 1) : try_pin(d, b);
        explicit_bzero(b, sizeof b);
        if (v == V_OPEN) return 0;
        int fd = open("/dev/console", O_WRONLY | O_CLOEXEC);
        if (fd >= 0) {
            dprintf(fd, "%s\n", v == V_WRONG ? "That didn't work. Try again." :
                    v == V_LOCKED ? "Too many tries. Wait about ten minutes, or type r and your rescue code." :
                    "This computer has changed. Type r and then your rescue code.");
            close(fd);
        }
    }
}

/* ── the keyboard and the pointer, straight from the kernel ───────── */

#define MAXDEV 16
typedef struct {
    int fd; char path[32];
    int absx_min, absx_max, absy_min, absy_max, is_abs;
} indev;
static indev dev[MAXDEV];
static int ndev;

#define BITS(n) (((n) + 8 * sizeof(long) - 1) / (8 * sizeof(long)))
static int bit(const unsigned long *b, int i) { return (int)((b[i / (8 * sizeof(long))] >> (i % (8 * sizeof(long)))) & 1); }

static void rescan_input(void)
{
    for (int n = 0; n < 32 && ndev < MAXDEV; n++) {
        char p[32];
        snprintf(p, sizeof p, "/dev/input/event%d", n);
        int have = 0;
        for (int i = 0; i < ndev; i++) if (!strcmp(dev[i].path, p)) have = 1;
        if (have) continue;
        int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        unsigned long keys[BITS(KEY_MAX + 1)] = { 0 }, abs[BITS(ABS_MAX + 1)] = { 0 };
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs);
        int useful = bit(keys, KEY_1) || bit(keys, KEY_KP1) || bit(keys, BTN_LEFT) || bit(keys, BTN_TOUCH);
        if (!useful) { close(fd); continue; }
        indev *d = &dev[ndev++];
        memset(d, 0, sizeof *d);
        d->fd = fd;
        snprintf(d->path, sizeof d->path, "%s", p);
        if (bit(abs, ABS_X) && bit(abs, ABS_Y)) {
            struct input_absinfo ax, ay;
            if (ioctl(fd, EVIOCGABS(ABS_X), &ax) == 0 && ioctl(fd, EVIOCGABS(ABS_Y), &ay) == 0 &&
                ax.maximum > ax.minimum && ay.maximum > ay.minimum) {
                d->is_abs = 1;
                d->absx_min = ax.minimum; d->absx_max = ax.maximum;
                d->absy_min = ay.minimum; d->absy_max = ay.maximum;
            }
        }
    }
}

static int key_of(int code)
{
    switch (code) {
    case KEY_1: case KEY_KP1: return 1;  case KEY_2: case KEY_KP2: return 2;
    case KEY_3: case KEY_KP3: return 3;  case KEY_4: case KEY_KP4: return 4;
    case KEY_5: case KEY_KP5: return 5;  case KEY_6: case KEY_KP6: return 6;
    case KEY_7: case KEY_KP7: return 7;  case KEY_8: case KEY_KP8: return 8;
    case KEY_9: case KEY_KP9: return 9;  case KEY_0: case KEY_KP0: return 0;
    case KEY_BACKSPACE: case KEY_DELETE: return K_DEL;
    case KEY_ENTER: case KEY_KPENTER: return K_OK;
    default: return -1;
    }
}

static int in_rect(rect r, int x, int y) { return r.w && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

static kms_display *disp;
static look *disp_look;
static geo   disp_geo;

static void show(app *a)
{
    surface *s = kms_back_surface(disp);
    if (s) { paint(s, a->u, disp_look, &disp_geo); kms_flip(disp); }
}

static int run_page(drive *d)
{
    look L;
    look_load(&L, NULL, disp->height);
    disp_look = &L;
    ui u;
    memset(&u, 0, sizeof u);
    u.pressed = -1;
    u.pin_len = d->pin_len;
    tpm_da da;
    u.has_tpm = tpm_da_read(&da) == 0;
    if (d->has_tpm_token && u.has_tpm && da.in_lockout) {
        u.wait_min = da.interval ? (da.interval + 59) / 60 : 10;
        u.page = P_LOCKED;
    }
    /* The key is in a chip that is not answering (switched off in the
     * firmware, or a different board): no PIN can work, so do not ask. */
    if (d->has_tpm_token && !u.has_tpm) { u.changed = 1; u.page = P_RESCUE; }
    app a = { d, &u, 0 };
    u.cx = disp->width / 2; u.cy = disp->height / 2;
    rescan_input();
    show(&a);
    diag("aurlock-pin: asking for the PIN on a %dx%d screen, %d input devices\n",
            disp->width, disp->height, ndev);
    time_t last_scan = time(NULL), last_poll = last_scan;
    struct timespec pressed_at = { 0, 0 };
    while (!a.opened) {
        struct pollfd p[MAXDEV];
        for (int i = 0; i < ndev; i++) p[i] = (struct pollfd){ dev[i].fd, POLLIN, 0 };
        int r = poll(p, (nfds_t)ndev, u.pressed >= 0 ? 150 : 1000);
        int dirty = 0;
        time_t now = time(NULL);
        if (now - last_scan >= 2) { rescan_input(); last_scan = now; }
        if (u.pressed >= 0 && r == 0) { u.pressed = -1; dirty = 1; }
        if (u.page == P_LOCKED && now - last_poll >= 10) {
            last_poll = now;
            if (tpm_da_read(&da) == 0 && !da.in_lockout) { go(&u, P_PIN, M_TRY_AGAIN_NOW); dirty = 1; }
        }
        for (int i = 0; r > 0 && i < ndev; i++) {
            if (!(p[i].revents & POLLIN)) continue;
            struct input_event ev[32];
            ssize_t n = read(dev[i].fd, ev, sizeof ev);
            if (n <= 0) {
                if (n < 0 && errno != EAGAIN) { close(dev[i].fd); dev[i] = dev[--ndev]; }
                continue;
            }
            for (int j = 0; j < (int)(n / (ssize_t)sizeof ev[0]); j++) {
                struct input_event *e = &ev[j];
                if (e->type == EV_REL) {
                    if (e->code == REL_X) u.cx += e->value;
                    if (e->code == REL_Y) u.cy += e->value;
                    u.cx = u.cx < 0 ? 0 : u.cx >= disp->width ? disp->width - 1 : u.cx;
                    u.cy = u.cy < 0 ? 0 : u.cy >= disp->height ? disp->height - 1 : u.cy;
                    u.show_cursor = 1; dirty = 1;
                } else if (e->type == EV_ABS && dev[i].is_abs &&
                           (e->code == ABS_X || e->code == ABS_Y)) {
                    if (e->code == ABS_X)
                        u.cx = (int)((long)(e->value - dev[i].absx_min) * (disp->width - 1) /
                                     (dev[i].absx_max - dev[i].absx_min));
                    else
                        u.cy = (int)((long)(e->value - dev[i].absy_min) * (disp->height - 1) /
                                     (dev[i].absy_max - dev[i].absy_min));
                    u.show_cursor = 1; dirty = 1;
                } else if (e->type == EV_KEY && e->value == 1) {
                    if (e->code == BTN_LEFT || e->code == BTN_TOUCH) {
                        for (int k = 0; k < NKEY; k++)
                            if (in_rect(disp_geo.key[k], u.cx, u.cy)) {
                                u.pressed = k; press(&a, k, show); dirty = 1;
                                clock_gettime(CLOCK_MONOTONIC, &pressed_at);
                            }
                        if (in_rect(disp_geo.link, u.cx, u.cy)) { link_pressed(&a); dirty = 1; }
                    } else {
                        int k = key_of(e->code);
                        if (k >= 0) { u.pressed = k; press(&a, k, show); dirty = 1; }
                    }
                }
            }
        }
        if (dirty) show(&a);
    }
    show(&a);
    return 0;
}

/* ── offline pictures, for docs/shots and the tests ───────────────── */

static int run_png(const char *out, int w, int h, const char *page, const char *theme, int typed)
{
    look L;
    look_load(&L, theme, h);
    ui u;
    memset(&u, 0, sizeof u);
    u.pressed = -1; u.has_tpm = 1; u.pin_len = 4; u.wait_min = 10;
    static const struct { const char *name; int page, msg, changed; } P[] = {
        { "pin", P_PIN, M_NONE, 0 },            { "pin-wrong", P_PIN, M_WRONG, 0 },
        { "locked", P_LOCKED, M_NONE, 0 },      { "rescue", P_RESCUE, M_NONE, 0 },
        { "changed", P_RESCUE, M_NONE, 1 },     { "rescue-wrong", P_RESCUE, M_RESCUE_WRONG, 0 },
        { "newpin", P_NEWPIN, M_NONE, 1 },      { "newpin-notpm", P_NEWPIN, M_NONE, 1 },
        { "newpin2", P_NEWPIN2, M_NONE, 1 },    { "different", P_NEWPIN, M_DIFFERENT, 1 },
        { "busy", P_BUSY, M_NONE, 0 },
    };
    int found = 0;
    for (size_t i = 0; i < sizeof P / sizeof P[0]; i++)
        if (!strcmp(P[i].name, page)) {
            u.page = P[i].page; u.msg = P[i].msg; u.changed = P[i].changed; found = 1;
            if (!strcmp(page, "newpin-notpm")) u.has_tpm = 0;
        }
    if (!found) { fprintf(stderr, "aurlock-pin: no page called %s\n", page); return 2; }
    const char *digits = "482109371156883022746619";
    for (int i = 0; i < typed && i < (u.page == P_RESCUE ? RESCUE_LEN : PIN_MAX); i++)
        u.typed[u.ntyped++] = digits[i];
    surface *s = surface_new(w, h);
    if (!s) return 1;
    geo g;
    paint(s, &u, &L, &g);
    /* the geometry, for the tests: every key and the button, and
     * whether each is at least 44 px on its shorter side */
    for (int i = 0; i < NKEY; i++)
        if (g.key[i].w) printf("key %d %d %d %d %d\n", i, g.key[i].x, g.key[i].y, g.key[i].w, g.key[i].h);
    if (g.link.w) printf("link %d %d %d %d\n", g.link.x, g.link.y, g.link.w, g.link.h);
    int rc = png_write_rgb(out, s->px, s->w, s->h);
    surface_free(s);
    return rc < 0 ? 1 : 0;
}

/* ── tests drive the page itself: pressing keys in --script ─────────
 *
 * "--script DEVICE NAME" reads key names from stdin ("1".."9", "0",
 * "del", "ok", "link") and runs exactly the page's own logic, printing
 * the page and message after each. It is how theftunit proves the
 * page's decisions without a screen. */
static void no_busy(app *a) { (void)a; }
static int run_script(drive *d)
{
    ui u;
    memset(&u, 0, sizeof u);
    u.pressed = -1; u.pin_len = d->pin_len;
    tpm_da da;
    u.has_tpm = tpm_da_read(&da) == 0;
    app a = { d, &u, 0 };
    char line[64];
    static const char *PN[] = { "pin", "locked", "rescue", "newpin", "newpin2", "busy", "done" };
    while (!a.opened && fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strcmp(line, "link")) link_pressed(&a);
        else if (!strcmp(line, "del")) press(&a, K_DEL, no_busy);
        else if (!strcmp(line, "ok")) press(&a, K_OK, no_busy);
        else for (char *c = line; *c; c++) if (*c >= '0' && *c <= '9') press(&a, *c - '0', no_busy);
        printf("page=%s msg=%d changed=%d typed=%d\n", PN[u.page], u.msg, u.changed, u.ntyped);
        fflush(stdout);
    }
    return a.opened ? 0 : 1;
}

/* lockctl: does this PIN open the drive (nothing is opened)? */
static int run_check(drive *d)
{
    char line[64];
    if (!fgets(line, sizeof line, stdin)) return 2;
    line[strcspn(line, "\r\n")] = 0;
    d->name = NULL;
    verdict v = try_pin(d, line);
    explicit_bzero(line, sizeof line);
    printf("%s\n", VNAME[v]);
    return v == V_OPEN ? 0 : 1;
}

/* lockctl: add a passphrase keyslot, authorised by the PIN (through the
 * chip when the key is sealed there). Line 1 the PIN, line 2 the new
 * key. Prints the new slot. This is the one thing systemd-cryptenroll
 * cannot do -- it needs a passphrase to start from -- and what lets a
 * PIN be changed without the rescue code. */
static int run_add_key(drive *d)
{
    char pin[64], key[128];
    if (!fgets(pin, sizeof pin, stdin) || !fgets(key, sizeof key, stdin)) return 2;
    pin[strcspn(pin, "\r\n")] = 0; key[strcspn(key, "\r\n")] = 0;
    if (!cs.kc_pass || !cs.kc_token || !cs.kc_add || !cs.kc_free || !*key) return 2;
    void *old = NULL, *new_ = NULL;
    tpm2_params p = { UINT32_MAX, test_tpm(), NULL, NULL };
    int r = d->has_tpm_token
        ? cs.kc_token(d->cd, CRYPT_ANY_TOKEN, "systemd-tpm2", pin, strlen(pin), p.device ? &p : NULL, &old)
        : cs.kc_pass(d->cd, pin, strlen(pin), &old);
    if (r >= 0) r = cs.kc_pass(d->cd, key, strlen(key), &new_);
    if (r >= 0) r = cs.kc_add(d->cd, CRYPT_ANY_SLOT, old, CRYPT_ANY_SLOT, new_, 0);
    if (old) cs.kc_free(old);
    if (new_) cs.kc_free(new_);
    explicit_bzero(pin, sizeof pin); explicit_bzero(key, sizeof key);
    if (r < 0) { fprintf(stderr, "aurlock-pin: could not add the key (%d)\n", r); return 1; }
    printf("%d\n", r);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "--tpm-status")) {
        tpm_da t;
        if (tpm_da_read(&t) < 0) { printf("tpm=no\n"); return 0; }
        printf("tpm=yes\ncounter=%u\nmax_tries=%u\ninterval=%u\nrecovery=%u\nin_lockout=%d\nlockout_auth_set=%d\n",
               t.counter, t.max_tries, t.interval, t.recovery, t.in_lockout, t.lockout_auth_set);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "--png")) {
        const char *out = NULL, *page = "pin", *theme = "/nonexistent";
        int w = 1280, h = 720, typed = 0;
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--png") && i + 1 < argc) out = argv[++i];
            else if (!strcmp(argv[i], "--size") && i + 2 < argc) { w = atoi(argv[++i]); h = atoi(argv[++i]); }
            else if (!strcmp(argv[i], "--page") && i + 1 < argc) page = argv[++i];
            else if (!strcmp(argv[i], "--theme") && i + 1 < argc) theme = argv[++i];
            else if (!strcmp(argv[i], "--typed") && i + 1 < argc) typed = atoi(argv[++i]);
        }
        if (!out || w < 320 || h < 240) { fprintf(stderr, "usage: aurlock-pin --png F [--size W H] [--page P]\n"); return 2; }
        return run_png(out, w, h, page, theme, typed);
    }
    int mode = 0;   /* 0 page, 1 try, 2 script, 3 check, 4 add-key */
    int i = 1;
    if (argc >= 2 && !strcmp(argv[1], "--try")) { mode = 1; i = 2; }
    if (argc >= 2 && !strcmp(argv[1], "--script")) { mode = 2; i = 2; }
    if (argc >= 2 && !strcmp(argv[1], "--check")) { mode = 3; i = 2; }
    if (argc >= 2 && !strcmp(argv[1], "--add-key")) { mode = 4; i = 2; }
    int want = mode >= 3 ? 1 : 2;
    if (argc - i != want) {
        fprintf(stderr, "usage: aurlock-pin [--try|--script] DEVICE NAME | --check|--add-key DEVICE\n");
        return 2;
    }
    drive d = { argv[i], want == 2 ? argv[i + 1] : NULL, NULL, 0, 0 };
    if (drive_load(&d) < 0) return 3;
    if (mode == 3) return run_check(&d);
    if (mode == 4) return run_add_key(&d);
    if (mode == 1) return run_try(&d);
    if (mode == 2) return try_temp(&d) ? 0 : run_script(&d);

    /* Before she has chosen a PIN, the drive just opens. */
    if (try_temp(&d)) return 0;

    signal(SIGPIPE, SIG_IGN);
    disp = kms_open(NULL);
    if (!disp) return run_console(&d);
    int rc = run_page(&d);
    kms_close(disp);
    return rc;
}
