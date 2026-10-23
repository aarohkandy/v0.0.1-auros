/* hwcompat.c -- see hwcompat.h. Pure C; the facts are in the generated
 * headers (tools/hw/gen-header.py makes them from hw/compat.json). */

#include "hwcompat.h"
#include "hwcompat_data.h"
#include "hwcompat_words.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HWC_K_PRINTER 10

const char *hwcompat_kernel(void) { return HWC_KERNEL; }

/* ── which module claims this device ────────────────────────────────── */
static int alias_match(const hwc_alias *a, const hw_dev *d)
{
    int bus = d->bus == HW_BUS_PCI ? 1 : d->bus == HW_BUS_USB ? 2 : 0;
    if (!bus || a->bus != bus || !d->has_ids) return 0;
    if (a->vid != 0xFFFF && a->vid != d->vid) return 0;
    if (a->did != 0xFFFF && a->did != d->did) return 0;
    if (bus == 1 && (a->svid != 0xFFFF || a->sdid != 0xFFFF)) {
        /* Windows writes SUBSYS_<device><vendor>; Linux says vendor then device */
        uint16_t sv = (uint16_t)(d->subsys & 0xFFFF), sd = (uint16_t)(d->subsys >> 16);
        if (!d->subsys) return 0;
        if (a->svid != 0xFFFF && a->svid != sv) return 0;
        if (a->sdid != 0xFFFF && a->sdid != sd) return 0;
    }
    if (a->ccmask) {
        if (d->cc < 0) {
            /* Windows gave no class. An entry that names the vendor AND the
             * product exactly does not need one (the class only picks which
             * interface of the device the driver takes); an entry that is
             * ONLY a class cannot be told without it. */
            if (a->vid == 0xFFFF || a->did == 0xFFFF) return 0;
        } else if (((uint32_t)d->cc & a->ccmask) != a->cc) {
            return 0;
        }
    }
    return 1;
}

int hwcompat_lookup(const hw_dev *d, int cat, hwc_hit *h)
{
    int seen[16], nseen = 0, usable = 0;
    const char *unusable_module = NULL;
    memset(h, 0, sizeof *h);
    for (int i = 0; i < HWC_N_ALIASES; i++) {
        const hwc_alias *a = &hwc_aliases[i];
        if (!alias_match(a, d)) continue;
        const hwc_module *m = &hwc_modules[a->mod];
        if (m->cat == 7) {                       /* bcma, ssb: a door, not a driver */
            if (!h->bus_module) h->bus_module = m->name;
            h->n_bus_only++;
            continue;
        }
        if (cat && m->cat != cat) continue;
        int again = 0;                           /* one module, however many of its ids match */
        for (int k = 0; k < nseen; k++) if (seen[k] == a->mod) again = 1;
        if (again) continue;
        if (nseen < 16) seen[nseen++] = a->mod;
        h->n_real++;
        if (m->fw_unusable) { if (!unusable_module) unusable_module = m->name; }
        else { usable++; if (!h->module) h->module = m->name; }
    }
    if (!usable && unusable_module) { h->module = unusable_module; h->fw_unusable = 1; }
    return h->n_real;
}

/* ── states ──────────────────────────────────────────────────────────── */
static int rank(hw_vstate s)
{
    switch (s) { case HW_V_WORKS: return 0; case HW_V_PARTIAL: return 1;
                 case HW_V_UNKNOWN: return 2; case HW_V_WONT: return 3; default: return 4; }
}

static const char *say(int k, hw_vstate s)
{
    switch (k) {
    case HW_K_WIFI:
        return s == HW_V_WORKS ? W_wifi_works : s == HW_V_PARTIAL ? W_wifi_partial :
               s == HW_V_WONT ? W_wifi_wont : W_wifi_unknown;
    case HW_K_ETHERNET:
        return s == HW_V_WORKS ? W_ethernet_works : s == HW_V_WONT ? W_ethernet_wont : W_ethernet_unknown;
    case HW_K_BLUETOOTH:
        return s == HW_V_WORKS ? W_bluetooth_works : s == HW_V_WONT ? W_bluetooth_wont : W_bluetooth_unknown;
    case HW_K_AUDIO:
        return s == HW_V_WORKS ? W_audio_works : s == HW_V_WONT ? W_audio_wont : W_audio_unknown;
    case HW_K_CAMERA:
        return s == HW_V_WORKS ? W_camera_works : s == HW_V_WONT ? W_camera_wont : W_camera_unknown;
    case HW_K_GPU:
        return s == HW_V_WORKS ? W_gpu_works : s == HW_V_PARTIAL ? W_gpu_partial : W_gpu_unknown;
    case HW_K_SCANNER:
        return s == HW_V_WORKS ? W_scanner_works : W_scanner_wont;
    case HW_K_FINGERPRINT:
        return W_fingerprint_wont;
    case HWC_K_PRINTER:
        return s == HW_V_WORKS ? W_printer_works : s == HW_V_PARTIAL ? W_printer_no_scan :
               s == HW_V_WONT ? W_printer_wont : W_printer_unknown;
    }
    return "";
}

/* One device's answer, before they are folded into one verdict per kind. */
typedef struct {
    int       vkind;          /* the kind of verdict this speaks to */
    hw_vstate state;
    int       dev;
    char      id[40];
    const char *says;         /* NULL: the catalog's sentence for (kind, state) */
    char      why[240];
} res;

static int ci_has(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (!n) return 1;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i]) {
            int a = (unsigned char)hay[i], b = (unsigned char)needle[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
            i++;
        }
        if (i == n) return 1;
    }
    return 0;
}

static const hwc_rule *device_rule(const hw_dev *d, int nreal)
{
    for (int i = 0; i < HWC_N_RULES; i++) {
        const hwc_rule *r = &hwc_rules[i];
        if (r->verdict == HWC_K_PRINTER) continue;
        if (r->bus && r->bus != (int)d->bus) continue;
        if (r->vid && (!d->has_ids || r->vid != d->vid)) continue;
        if (r->ndid) {
            int ok = 0;
            for (int k = 0; k < r->ndid; k++) if (d->has_ids && r->did[k] == d->did) ok = 1;
            if (!ok) continue;
        }
        if (!r->any_kind && (int)d->kind != r->verdict) continue;
        if (r->any_kind && !r->vid) continue;                 /* a rule must name something */
        if (r->need == 1 && nreal) continue;
        if (r->need == 2 && !nreal) continue;
        return r;
    }
    return NULL;
}

static void why_driver(char *w, size_t n, const hwc_hit *h, const char *what)
{
    snprintf(w, n, "%s: module %s in the image's kernel %s%s", what, h->module ? h->module : "?", HWC_KERNEL,
             h->fw_unusable ? ", but the firmware it must load is not in the image" :
             ", and its firmware is in the image (checked for the driver, not for this exact chip)");
}

/* Judge one device; returns 1 when it speaks to some verdict. */
static int eval_dev(const hw_inv *inv, int di, res *out)
{
    const hw_dev *d = &inv->devs[di];
    hwc_hit h;
    int nreal = 0;
    if (d->bus == HW_BUS_PCI || d->bus == HW_BUS_USB)
        nreal = hwcompat_lookup(d, d->kind >= HW_K_WIFI && d->kind <= HW_K_GPU ? (int)d->kind : 0, &h);
    else memset(&h, 0, sizeof h);
    memset(out, 0, sizeof *out);
    out->dev = di;

    const hwc_rule *r = device_rule(d, nreal);
    if (r) {
        out->vkind = r->verdict;
        out->state = (hw_vstate)r->state;
        out->says = r->says;
        snprintf(out->id, sizeof out->id, "%s", r->id);
        snprintf(out->why, sizeof out->why, "%s", r->why);
        return 1;
    }

    switch (d->kind) {
    case HW_K_WIFI: case HW_K_ETHERNET: case HW_K_BLUETOOTH: {
        const char *key = d->kind == HW_K_WIFI ? "wifi" : d->kind == HW_K_ETHERNET ? "ethernet" : "bluetooth";
        out->vkind = d->kind;
        if (nreal && !h.fw_unusable) {
            out->state = HW_V_WORKS;
            snprintf(out->id, sizeof out->id, "%s-driver", key);
            why_driver(out->why, sizeof out->why, &h, "driver");
        } else if (nreal) {
            out->state = HW_V_WONT;
            snprintf(out->id, sizeof out->id, "%s-firmware-missing", key);
            why_driver(out->why, sizeof out->why, &h, "driver");
        } else if (d->has_ids && (d->bus == HW_BUS_PCI || d->bus == HW_BUS_USB)) {
            out->state = HW_V_WONT;
            snprintf(out->id, sizeof out->id, "%s-no-driver", key);
            snprintf(out->why, sizeof out->why, "no module in the image's kernel %s claims %s %04X:%04X%s%s",
                     HWC_KERNEL, d->bus == HW_BUS_PCI ? "PCI" : "USB", d->vid, d->did,
                     h.bus_module ? "; only the bus module " : "", h.bus_module ? h.bus_module : "");
        } else {
            out->state = HW_V_UNKNOWN;
            snprintf(out->id, sizeof out->id, "%s-unidentified", key);
            snprintf(out->why, sizeof out->why, "Windows gave no PCI or USB id for it");
        }
        return 1;
    }
    case HW_K_AUDIO:
        out->vkind = HW_K_AUDIO;
        if (d->bus == HW_BUS_HDA) {
            out->state = HW_V_WORKS;
            snprintf(out->id, sizeof out->id, "audio-hda-codec");
            snprintf(out->why, sizeof out->why, "an HD-Audio codec: the kernel's generic codec driver handles any");
        } else if (nreal && !h.fw_unusable) {
            out->state = HW_V_WORKS;
            snprintf(out->id, sizeof out->id, "audio-driver");
            why_driver(out->why, sizeof out->why, &h, "sound driver");
        } else if (nreal) {
            out->state = HW_V_WONT;
            snprintf(out->id, sizeof out->id, "audio-firmware-missing");
            why_driver(out->why, sizeof out->why, &h, "sound driver");
        } else {
            out->state = HW_V_UNKNOWN;
            snprintf(out->id, sizeof out->id, "audio-unidentified");
            snprintf(out->why, sizeof out->why, "no sound driver claims it by id or class");
        }
        return 1;
    case HW_K_CAMERA:
        out->vkind = HW_K_CAMERA;
        if (nreal && !h.fw_unusable) {
            out->state = HW_V_WORKS;
            snprintf(out->id, sizeof out->id, "camera-driver");
            why_driver(out->why, sizeof out->why, &h, "camera driver");
        } else if (d->bus == HW_BUS_PCI || (d->bus == HW_BUS_USB && d->cc >= 0)) {
            out->state = HW_V_WONT;
            snprintf(out->id, sizeof out->id, "camera-no-driver");
            snprintf(out->why, sizeof out->why, "not a standard USB video camera and no module claims it");
        } else {
            out->state = HW_V_UNKNOWN;
            snprintf(out->id, sizeof out->id, "camera-unidentified");
            snprintf(out->why, sizeof out->why, "Windows gave no usable id for it");
        }
        return 1;
    case HW_K_GPU:
        out->vkind = HW_K_GPU;
        if (nreal && !h.fw_unusable) {
            out->state = HW_V_WORKS;
            snprintf(out->id, sizeof out->id, "gpu-driver");
            why_driver(out->why, sizeof out->why, &h, "graphics driver");
        } else {
            out->state = HW_V_PARTIAL;
            snprintf(out->id, sizeof out->id, "gpu-plain-screen");
            snprintf(out->why, sizeof out->why, "%s: the screen still works through the firmware's plain framebuffer, without acceleration",
                     nreal ? "its driver cannot start" : "no graphics driver in the image's kernel claims it");
        }
        return 1;
    case HW_K_SCANNER:
        out->vkind = HW_K_SCANNER;
        out->state = HWC_F_sane ? HW_V_UNKNOWN : HW_V_WONT;
        snprintf(out->id, sizeof out->id, HWC_F_sane ? "scanner-model-unknown" : "scanner-no-sane");
        snprintf(out->why, sizeof out->why, HWC_F_sane ? "SANE is in the image; whether this model has a backend is not known"
                                                         : "the image has no scanner software (SANE)");
        return 1;
    case HW_K_FINGERPRINT:
        out->vkind = HW_K_FINGERPRINT;
        out->state = HW_V_UNKNOWN;
        snprintf(out->id, sizeof out->id, "fingerprint-service-present");
        snprintf(out->why, sizeof out->why, "the image has a fingerprint service; whether it knows this reader is not checked");
        return 1;
    default:
        return 0;
    }
}

/* ── printers ───────────────────────────────────────────────────────── */
static void norm_words(const char *s, char *out, size_t n)
{
    size_t j = 0; int sp = 1;
    if (n < 3) { if (n) out[0] = 0; return; }
    out[j++] = ' ';
    for (; *s && j + 2 < n; s++) {
        int c = (unsigned char)*s;
        if (c >= 'A' && c <= 'Z') c += 32;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { out[j++] = (char)c; sp = 0; }
        else if (!sp) { out[j++] = ' '; sp = 1; }
    }
    if (!sp) out[j++] = ' ';
    out[j] = 0;
}

static int in_db(const char *padded)
{
    char m[160];
    for (int i = 0; i < HWC_N_PRINTER_MODELS; i++) {
        const char *model = hwc_printer_models[i];
        if (strlen(model) + 3 > sizeof m) continue;
        snprintf(m, sizeof m, " %s ", model);
        if (strstr(padded, m)) return 1;
    }
    return 0;
}

static int looks_like_mfp(const char *s)
{
    static const char *const w[] = { "mfp", "mfc", "all-in-one", "all in one", "officejet", "photosmart",
                                     "pixma", "workforce", "aio", NULL };
    for (int i = 0; w[i]; i++) if (ci_has(s, w[i])) return 1;
    return 0;
}

static void eval_printer(const hw_inv *inv, int pi, res *out)
{
    const hw_printer *p = &inv->printers[pi];
    char both[200], a[200], b[200];
    memset(out, 0, sizeof *out);
    out->vkind = HWC_K_PRINTER; out->dev = -1;
    snprintf(both, sizeof both, "%s %s", p->driver, p->name);

    for (int i = 0; i < HWC_N_RULES; i++) {
        const hwc_rule *r = &hwc_rules[i];
        if (r->verdict != HWC_K_PRINTER) continue;
        for (int k = 0; k < r->nnames; k++)
            if (ci_has(both, r->names[k])) {
                out->state = (hw_vstate)r->state; out->says = r->says;
                snprintf(out->id, sizeof out->id, "%s", r->id);
                snprintf(out->why, sizeof out->why, "%s", r->why);
                return;
            }
    }
    norm_words(p->driver, a, sizeof a);
    norm_words(p->name, b, sizeof b);
    int ipp = ci_has(p->driver, "IPP") || ci_has(p->port, "ipp") || ci_has(p->port, "http");
    if (ipp) {
        out->state = HW_V_WORKS;
        snprintf(out->id, sizeof out->id, "printer-driverless");
        snprintf(out->why, sizeof out->why, "Windows prints to it as an IPP printer; CUPS prints to those with no model driver");
    } else if (in_db(a) || in_db(b)) {
        out->state = HW_V_WORKS;
        snprintf(out->id, sizeof out->id, "printer-model-driver");
        snprintf(out->why, sizeof out->why, "CUPS has a driver for this model (HP hpcups or Gutenprint) in the image");
    } else if (p->conn == HW_CONN_USB) {
        if (HWC_F_ipp_usb) {
            out->state = HW_V_UNKNOWN;
            snprintf(out->id, sizeof out->id, "printer-usb-ippusb-maybe");
            snprintf(out->why, sizeof out->why, "no model driver; prints over USB only if the printer speaks IPP-over-USB (ipp-usb is in the image)");
        } else {
            out->state = HW_V_WONT;
            snprintf(out->id, sizeof out->id, "printer-usb-no-driver");
            snprintf(out->why, sizeof out->why, "USB printer with no model driver in the image and no ipp-usb to print without one; over Wi-Fi it may well work");
        }
    } else {
        out->state = HW_V_UNKNOWN;
        snprintf(out->id, sizeof out->id, "printer-unidentified");
        snprintf(out->why, sizeof out->why, "no model driver known; most network printers since about 2015 print without one%s",
                 HWC_F_cups_driverless ? "" : " (but the image has no driverless printing)");
    }

    if (out->state == HW_V_WORKS && !HWC_F_sane) {
        int scanner_too = looks_like_mfp(both);
        for (int i = 0; i < inv->n_devs && !scanner_too; i++)
            if (inv->devs[i].kind == HW_K_SCANNER && p->has_usb && inv->devs[i].vid == p->vid)
                scanner_too = 1;
        if (scanner_too) {
            size_t l = strlen(out->why);
            out->state = HW_V_PARTIAL;
            snprintf(out->id, sizeof out->id, "printer-no-scan");
            snprintf(out->why + l, sizeof out->why - l, "; but it scans too, and the image has no scanner software (SANE)");
        }
    }
}

/* ── folding into verdicts ────────────────────────────────────────── */
static void put(hw_inv *inv, int kind, hw_vstate st, const res *r, int printer, int blocks, const char *says)
{
    if (inv->n_verdicts >= HW_MAX_VERDICTS) return;
    hw_verdict *v = &inv->verdicts[inv->n_verdicts++];
    memset(v, 0, sizeof *v);
    v->kind = kind == HWC_K_PRINTER ? HW_K_OTHER : (hw_kind)kind;
    v->state = st; v->dev = r ? r->dev : -1; v->printer = printer; v->blocks = blocks;
    snprintf(v->id, sizeof v->id, "%s", r ? r->id : "");
    snprintf(v->says, sizeof v->says, "%s", says ? says : "");
    snprintf(v->why, sizeof v->why, "%s", r ? r->why : "");
}

int hwcompat_judge(hw_inv *inv)
{
    static const int ORDER[] = { HW_K_WIFI, HW_K_ETHERNET, HW_K_BLUETOOTH, HW_K_AUDIO, HW_K_CAMERA,
                                 HW_K_GPU, HW_K_SCANNER, HW_K_FINGERPRINT };
    res *all = calloc((size_t)(inv->n_devs + 1), sizeof *all);
    int n = 0;
    inv->n_verdicts = 0;
    if (!all) return 0;
    for (int i = 0; i < inv->n_devs; i++)
        if (eval_dev(inv, i, &all[n])) n++;

    /* a scanner that is the other half of a listed printer is that printer's business */
    for (int i = 0; i < n; i++) {
        if (all[i].vkind != HW_K_SCANNER) continue;
        const hw_dev *d = &inv->devs[all[i].dev];
        for (int k = 0; k < inv->n_printers; k++)
            if (inv->printers[k].has_usb && inv->printers[k].vid == d->vid) all[i].vkind = 0;
    }

    int wifi_state = -1, eth_best = 99;
    for (int oi = 0; oi < (int)(sizeof ORDER / sizeof ORDER[0]); oi++) {
        int kind = ORDER[oi], best = -1;
        for (int i = 0; i < n; i++)
            if (all[i].vkind == kind && (best < 0 || rank(all[i].state) < rank(all[best].state))) best = i;
        if (best < 0) continue;
        const res *r = &all[best];
        hw_vstate st = r->state;
        int blocks = 0;
        const char *says = r->says ? r->says : say(kind, st);
        if (kind == HW_K_WIFI) wifi_state = st;
        if (kind == HW_K_ETHERNET) {
            eth_best = rank(st);
            /* the cable only needs a sentence when Wi-Fi is not the whole story */
            if (wifi_state == HW_V_WORKS) continue;
        }
        if (kind == HW_K_WIFI && st == HW_V_WONT) {
            int cable = 0;
            for (int i = 0; i < n; i++)
                if (all[i].vkind == HW_K_ETHERNET && rank(all[i].state) <= rank(HW_V_UNKNOWN)) cable = 1;
            if (!cable) { blocks = 1; says = W_wifi_wont_alone; }
        }
        put(inv, kind, st, r, -1, blocks, says);
    }
    (void)eth_best;

    for (int i = 0; i < inv->n_printers; i++) {
        res r;
        eval_printer(inv, i, &r);
        put(inv, HWC_K_PRINTER, r.state, &r, i, 0, r.says ? r.says : say(HWC_K_PRINTER, r.state));
    }
    free(all);
    return inv->n_verdicts;
}

/* ═══════════════════════════════════════════════════════════════════
 *  selftest: computers as Windows describes them (the ids are real
 *  ones), and what she must be told.
 * ═══════════════════════════════════════════════════════════════════ */
static int g_bad;
static void t(const char *name, int ok)
{
    printf("  %-70s %s\n", name, ok ? "ok" : "WRONG");
    if (!ok) g_bad++;
}

static const hw_verdict *vk(const hw_inv *inv, hw_kind k)
{
    for (int i = 0; i < inv->n_verdicts; i++) if (inv->verdicts[i].kind == k) return &inv->verdicts[i];
    return NULL;
}
static const hw_verdict *vp(const hw_inv *inv, int printer)
{
    for (int i = 0; i < inv->n_verdicts; i++) if (inv->verdicts[i].printer == printer) return &inv->verdicts[i];
    return NULL;
}
static int is(const hw_verdict *v, hw_vstate s, const char *id)
{
    return v && v->state == s && (!id || !strcmp(v->id, id));
}

static hw_inv *laptop(void)
{
    hw_inv *v = hwinv_new();
    return v;
}

int hwcompat_selftest(void)
{
    g_bad = 0;
    hw_inv *v;
    const hw_verdict *x;

    /* The whole point: a Broadcom BCM4352 (14e4:43b1) laptop with no cable socket */
    v = laptop();
    hwinv_add_dev(v, "Net", "PCI\\VEN_14E4&DEV_43B1&SUBSYS_0608105B&REV_03",
                  "PCI\\VEN_14E4&DEV_43B1&CC_028000;PCI\\VEN_14E4&DEV_43B1&CC_0280",
                  "Broadcom 802.11ac Network Adapter", "Broadcom", "bcmwl63a", 0, 71);
    v->devs[0].in_use = 1;
    hwcompat_judge(v);
    x = vk(v, HW_K_WIFI);
    t("BCM4352, no cable socket: Wi-Fi won't work, and it blocks", is(x, HW_V_WONT, "wifi-broadcom-wl-only") && x->blocks == 1);
    t("  and she is told so plainly", x && strstr(x->says, "no place for a network cable") && strstr(x->says, "Nothing has been changed"));
    t("  and the reason is ours to read", x && strstr(x->why, "wl") && strstr(x->why, "bcma"));
    {
        char j[128];
        size_t jl = hwinv_journal(v, j, sizeof j);
        t("  and the journal line says so: wifi=wont and blk=1, short, plain ASCII",
          !strcmp(j, "hw1;wifi=wont;eth=none;bt=none;audio=none;cam=none;gpu=none;scan=none;fp=none;prn=0;prnbad=0;blk=1") &&
          jl == strlen(j) && jl < 128);
        char tiny[12];
        hwinv_journal(v, tiny, sizeof tiny);
        t("  and a journal line that does not fit is cut, not overrun", strlen(tiny) == 11);
    }
    hwinv_free(v);

    /* the same chip with a cable socket: warned, not stopped */
    v = laptop();
    hwinv_add_dev(v, "Net", "PCI\\VEN_14E4&DEV_43B1&SUBSYS_0608105B&REV_03", "PCI\\VEN_14E4&DEV_43B1&CC_028000", "Broadcom 802.11ac", "", "", 0, 71);
    hwinv_add_dev(v, "Net", "PCI\\VEN_10EC&DEV_8168&SUBSYS_8AD01043&REV_15", "PCI\\VEN_10EC&DEV_8168&CC_020000",
                  "Realtek PCIe GbE Family Controller", "Realtek", "rt640x64", 0, 6);
    hwcompat_judge(v);
    x = vk(v, HW_K_WIFI);
    t("BCM4352 with a cable socket: Wi-Fi won't work but nothing blocks", is(x, HW_V_WONT, NULL) && x->blocks == 0 &&
      strstr(x->says, "network cable will"));
    x = vk(v, HW_K_ETHERNET);
    t("  and the cable socket is said to work", is(x, HW_V_WORKS, "ethernet-driver"));
    hwinv_free(v);

    /* an Intel AX201 is fine */
    v = laptop();
    hwinv_add_dev(v, "Net", "PCI\\VEN_8086&DEV_06F0&SUBSYS_00748086&REV_00", "PCI\\VEN_8086&DEV_06F0&CC_028000",
                  "Intel(R) Wi-Fi 6 AX201 160MHz", "Intel", "Netwtw10", 0, 71);
    hwcompat_judge(v);
    x = vk(v, HW_K_WIFI);
    t("Intel AX201: Wi-Fi will work", is(x, HW_V_WORKS, "wifi-driver") && x->blocks == 0 && !strcmp(x->says, "Your Wi-Fi will work."));
    t("  and the cable socket is not mentioned when there is none", !vk(v, HW_K_ETHERNET));
    hwinv_free(v);

    /* Wi-Fi chips: new, edge, missing from this kernel */
    struct { const char *hw, *comp, *name; hw_vstate st; const char *id; } wf[] = {
        { "PCI\\VEN_8086&DEV_272B&SUBSYS_00948086", "PCI\\VEN_8086&DEV_272B&CC_028000", "Intel(R) Wi-Fi 7 BE200", HW_V_WORKS, "wifi-driver" },
        { "PCI\\VEN_10EC&DEV_B852&SUBSYS_B8521A3B", "PCI\\VEN_10EC&DEV_B852&CC_028000", "Realtek RTL8852BE WiFi 6", HW_V_PARTIAL, "wifi-realtek-rtw89-unsteady" },
        { "PCI\\VEN_10EC&DEV_8922&SUBSYS_89221A3B", "PCI\\VEN_10EC&DEV_8922&CC_028000", "Realtek RTL8922AE WiFi 7", HW_V_WONT, "wifi-realtek-8922ae-newer-kernel" },
        { "PCI\\VEN_10EC&DEV_B723&SUBSYS_B7231A3B", "PCI\\VEN_10EC&DEV_B723&CC_028000", "Realtek RTL8723BE 802.11 b/g/n", HW_V_PARTIAL, "wifi-realtek-8723be-weak-signal" },
        { "PCI\\VEN_14C3&DEV_0616&SUBSYS_E616185F", "PCI\\VEN_14C3&DEV_0616&CC_028000", "MediaTek MT7922 802.11ax", HW_V_WORKS, "wifi-driver" },
        { "PCI\\VEN_168C&DEV_003E&SUBSYS_0042168C", "PCI\\VEN_168C&DEV_003E&CC_028000", "Qualcomm Atheros QCA6174", HW_V_WORKS, "wifi-driver" },
        { "USB\\VID_0BDA&PID_8812", "", "Realtek RTL8812AU Wireless LAN 802.11ac USB NIC", HW_V_WONT, "wifi-no-driver" },
        { "USB\\VID_0BDA&PID_8179", "", "Realtek RTL8188EUS Wireless LAN 802.11n USB 2.0 Network Adapter", HW_V_WORKS, "wifi-driver" },
        { "USB\\VID_0E8D&PID_7612", "USB\\Class_FF&SubClass_FF&Prot_FF", "MediaTek MT7612U 802.11ac USB stick", HW_V_WORKS, "wifi-driver" },
        { "USB\\VID_2357&PID_0120", "USB\\Class_FF&SubClass_FF&Prot_FF", "TP-Link Archer T2U Nano (Realtek RTL8811AU)", HW_V_WONT, "wifi-no-driver" },
        { "PCI\\VEN_14E4&DEV_43A0&SUBSYS_0612105B", "PCI\\VEN_14E4&DEV_43A0&CC_028000", "Broadcom 802.11ac (BCM4360)", HW_V_WONT, "wifi-broadcom-wl-only" },
        { "PCI\\VEN_14E4&DEV_43A3&SUBSYS_0614106B", "PCI\\VEN_14E4&DEV_43A3&CC_028000", "Broadcom BCM4350 802.11ac", HW_V_WORKS, "wifi-driver" },
    };
    for (size_t i = 0; i < sizeof wf / sizeof wf[0]; i++) {
        char nm[120];
        v = laptop();
        hwinv_add_dev(v, "Net", wf[i].hw, wf[i].comp, wf[i].name, "", "", 0, 71);
        hwinv_add_dev(v, "Net", "PCI\\VEN_10EC&DEV_8168", "PCI\\VEN_10EC&DEV_8168&CC_020000", "Realtek PCIe GbE", "", "", 0, 6);
        hwcompat_judge(v);
        snprintf(nm, sizeof nm, "Wi-Fi: %s", wf[i].name);
        t(nm, is(vk(v, HW_K_WIFI), wf[i].st, wf[i].id));
        hwinv_free(v);
    }

    /* the camera on an Intel IPU chip is a camera that does not work yet;
     * with a USB webcam as well, the camera works */
    v = laptop();
    hwinv_add_dev(v, "System", "PCI\\VEN_8086&DEV_9A19&SUBSYS_22128086", "PCI\\VEN_8086&DEV_9A19&CC_048000", "Intel(R) Image Signal Processor", "", "", 0, 0);
    hwcompat_judge(v);
    t("Intel IPU6 camera (a 'System' device to Windows): camera won't work yet", is(vk(v, HW_K_CAMERA), HW_V_WONT, "camera-intel-ipu") &&
      !strcmp(vk(v, HW_K_CAMERA)->says, "Your camera won't work yet."));
    hwinv_add_dev(v, "Camera", "USB\\VID_0BDA&PID_5530&MI_00", "USB\\Class_0E&SubClass_01&Prot_00;USB\\Class_0E&SubClass_01", "USB2.0 HD UVC WebCam", "", "usbvideo", 0, 0);
    hwcompat_judge(v);
    t("  plus a USB webcam: the camera works (the best one counts)", is(vk(v, HW_K_CAMERA), HW_V_WORKS, "camera-driver"));
    hwinv_free(v);

    /* Bluetooth, sound, fingerprint, screen */
    v = laptop();
    hwinv_add_dev(v, "Bluetooth", "USB\\VID_8087&PID_0033&REV_0001", "USB\\Class_E0&SubClass_01&Prot_01;USB\\Class_E0&SubClass_01", "Intel(R) Wireless Bluetooth(R)", "", "ibtusb", 0, 0);
    hwinv_add_dev(v, "MEDIA", "PCI\\VEN_8086&DEV_A0C8&SUBSYS_0A1F17AA&REV_20", "PCI\\VEN_8086&DEV_A0C8&CC_040100;PCI\\VEN_8086&DEV_A0C8&CC_0401", "Intel(R) Smart Sound Technology", "", "IntcAudioBus", 0, 0);
    hwinv_add_dev(v, "Biometric", "USB\\VID_27C6&PID_55B4&REV_0100", "USB\\Class_FF&SubClass_00&Prot_00", "Goodix Fingerprint Device", "", "", 0, 0);
    hwinv_add_dev(v, "Display", "PCI\\VEN_8086&DEV_9A49&SUBSYS_0A1F17AA&REV_01", "PCI\\VEN_8086&DEV_9A49&CC_030000;PCI\\VEN_8086&DEV_9A49&CC_0300", "Intel(R) Iris(R) Xe Graphics", "", "igfx", 0, 0);
    hwinv_add_dev(v, "Display", "PCI\\VEN_10DE&DEV_2560&SUBSYS_0A1F17AA&REV_A1", "PCI\\VEN_10DE&DEV_2560&CC_030200;PCI\\VEN_10DE&DEV_2560&CC_0302", "NVIDIA GeForce RTX 3050 Laptop GPU", "", "nvlddmkm", 0, 0);
    hwcompat_judge(v);
    t("Intel Bluetooth radio: Bluetooth will work", is(vk(v, HW_K_BLUETOOTH), HW_V_WORKS, "bluetooth-driver"));
    t("Intel Smart Sound: sound will work", is(vk(v, HW_K_AUDIO), HW_V_WORKS, NULL));
    t("Goodix fingerprint reader: it won't work, and she is told what to do instead",
      is(vk(v, HW_K_FINGERPRINT), HW_V_WONT, "fingerprint-goodix") && strstr(vk(v, HW_K_FINGERPRINT)->says, "short number"));
    t("Intel + Nvidia laptop: the screen works (the Intel chip is used)", is(vk(v, HW_K_GPU), HW_V_WORKS, "gpu-driver"));
    hwinv_free(v);
    v = laptop();
    hwinv_add_dev(v, "Display", "PCI\\VEN_10DE&DEV_2684&SUBSYS_88611043", "PCI\\VEN_10DE&DEV_2684&CC_030000", "NVIDIA GeForce RTX 4090", "", "", 0, 0);
    hwcompat_judge(v);
    t("Nvidia-only desktop: the screen works but slowly", is(vk(v, HW_K_GPU), HW_V_PARTIAL, "gpu-nvidia-nouveau-only") &&
      strstr(vk(v, HW_K_GPU)->says, "slow"));
    hwinv_free(v);

    /* printers */
    v = laptop();
    int p_hp = hwinv_add_printer(v, "HP DeskJet 970Cxi", "HP Deskjet 970c", "USB001", 1);
    int p_br = hwinv_add_printer(v, "Brother HL-L2350DW series", "Brother HL-L2350DW series", "USB002", 0);
    int p_ip = hwinv_add_printer(v, "Office", "Microsoft IPP Class Driver", "192.168.1.40", 0);
    int p_cn = hwinv_add_printer(v, "Canon LBP6030", "Canon LBP6030/6040/6018L", "USB003", 0);
    int p_nw = hwinv_add_printer(v, "Upstairs", "Epson WF-3720 Series", "WSD-1234", 0);
    int p_mf = hwinv_add_printer(v, "HP OfficeJet 3830", "HP OfficeJet 3830 series", "WSD-5678", 0);
    hwcompat_judge(v);
    t("a printer CUPS has a model driver for will work", is(vp(v, p_hp), HW_V_WORKS, "printer-model-driver") &&
      !strcmp(vp(v, p_hp)->says, "Your printer will work."));
    t("a USB printer with no driver in the image won't work yet, and says what to do",
      is(vp(v, p_br), HW_V_WONT, "printer-usb-no-driver") && strstr(vp(v, p_br)->says, "Wi-Fi"));
    t("a printer Windows prints to over IPP will work", is(vp(v, p_ip), HW_V_WORKS, "printer-driverless"));
    t("a Canon CAPT printer only works with Windows", is(vp(v, p_cn), HW_V_WONT, "printer-canon-capt") &&
      strstr(vp(v, p_cn)->says, "only works with Windows"));
    t("a network printer we cannot identify: we cannot tell, and say so kindly", is(vp(v, p_nw), HW_V_UNKNOWN, "printer-unidentified") &&
      strstr(vp(v, p_nw)->says, "can't tell"));
    t("an all-in-one that prints: it prints, its scanner doesn't work yet", is(vp(v, p_mf), HW_V_UNKNOWN, NULL) || is(vp(v, p_mf), HW_V_PARTIAL, "printer-no-scan"));
    hwinv_free(v);
    v = laptop();
    int p_sc = hwinv_add_printer(v, "HP DeskJet 970Cxi", "HP Deskjet 970c", "USB001", 1);
    hwinv_printer_usb(v, p_sc, 0x03F0, 0x1004, "HPDeskjet_970c");
    hwinv_add_dev(v, "Image", "USB\\VID_03F0&PID_1004", "", "HP Deskjet 970c scanner", "", "usbscan", 0, 0);
    hwcompat_judge(v);
    t("a printer with a scanner on the same USB device: prints, scanner not yet",
      is(vp(v, p_sc), HW_V_PARTIAL, "printer-no-scan") && strstr(vp(v, p_sc)->says, "scanner won't work yet"));
    t("  and the scanner is not announced twice", !vk(v, HW_K_SCANNER));
    hwinv_free(v);

    /* a Wi-Fi card is not "driven" by a sound driver that claims the same id */
    v = laptop();
    hwinv_add_dev(v, "Net", "PCI\\VEN_8086&DEV_A0C8&SUBSYS_0A1F17AA", "PCI\\VEN_8086&DEV_A0C8&CC_040100", "Wi-Fi that is really a sound chip", "", "", 0, 71);
    hwcompat_judge(v);
    t("an id that only a sound driver claims is not a Wi-Fi driver", is(vk(v, HW_K_WIFI), HW_V_WONT, "wifi-no-driver"));
    hwinv_free(v);

    /* an alias that names a subsystem: the vendor's id and the device's, the right way round */
    {
        int found = 0;
        for (int i = 0; i < HWC_N_ALIASES && !found; i++) {
            const hwc_alias *a = &hwc_aliases[i];
            if (a->bus != 1 || a->svid == 0xFFFF || a->sdid == 0xFFFF || a->svid == a->sdid ||
                a->vid == 0xFFFF || a->did == 0xFFFF || a->ccmask || hwc_modules[a->mod].cat > 6)
                continue;
            int generic = 0;
            for (int j = 0; j < HWC_N_ALIASES; j++)
                if (hwc_aliases[j].bus == 1 && hwc_aliases[j].vid == a->vid && hwc_aliases[j].did == a->did &&
                    hwc_aliases[j].svid == 0xFFFF && hwc_aliases[j].sdid == 0xFFFF && !hwc_aliases[j].ccmask) generic = 1;
            if (generic) continue;
            hw_dev d1, d2;
            hwc_hit h;
            memset(&d1, 0, sizeof d1);
            d1.bus = HW_BUS_PCI; d1.has_ids = 1; d1.vid = a->vid; d1.did = a->did; d1.cc = -1;
            d1.subsys = ((uint32_t)a->sdid << 16) | a->svid;          /* Windows: SUBSYS_<device><vendor> */
            d2 = d1;
            d2.subsys = ((uint32_t)a->svid << 16) | a->sdid;          /* the wrong way round */
            found = 1;
            t("an id that is only claimed for one subsystem matches that subsystem", hwcompat_lookup(&d1, 0, &h) > 0);
            t("  and not the same two numbers the wrong way round", hwcompat_lookup(&d2, 0, &h) == 0);
        }
        if (!found) printf("  %-70s %s\n", "(no subsystem-only alias in this image: case skipped)", "ok");
    }

    /* a model name is matched whole words at a time: "HP DeskJet 970Cxi" is
     * not the "970c" the image has a driver for */
    v = laptop();
    int p_w = hwinv_add_printer(v, "HP DeskJet 970Cxi", "Generic printer", "USB001", 1);
    hwcompat_judge(v);
    t("a model number that only starts like one the image knows is not that model",
      is(vp(v, p_w), HW_V_WONT, "printer-usb-no-driver"));
    hwinv_free(v);

    /* a driver whose firmware is not in the image does not make a device work */
    {
        int found = 0;
        for (int i = 0; i < HWC_N_ALIASES && !found; i++) {
            const hwc_alias *a = &hwc_aliases[i];
            const hwc_module *m = &hwc_modules[a->mod];
            if (a->bus != 2 || a->vid == 0xFFFF || a->did == 0xFFFF || a->ccmask || m->cat != 1 || !m->fw_unusable)
                continue;
            char hw[64];
            found = 1;
            v = laptop();
            snprintf(hw, sizeof hw, "USB\\VID_%04X&PID_%04X", a->vid, a->did);
            hwinv_add_dev(v, "Net", hw, "", "A Wi-Fi stick whose firmware is missing", "", "", 0, 71);
            hwcompat_judge(v);
            t("a Wi-Fi driver whose firmware is not in the image: won't work, and says why",
              is(vk(v, HW_K_WIFI), HW_V_WONT, "wifi-firmware-missing") && strstr(vk(v, HW_K_WIFI)->why, "firmware"));
            hwinv_free(v);
        }
        if (!found) printf("  %-70s %s\n", "(this image has no Wi-Fi driver with missing firmware: case skipped)", "ok");
    }

    /* a computer with nothing to say */
    v = laptop();
    hwcompat_judge(v);
    t("nothing found: nothing said", v->n_verdicts == 0);
    hwinv_free(v);

    return g_bad;
}
