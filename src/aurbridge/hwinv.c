/* hwinv.c -- the pure half of the hardware inventory. See hwinv.h.
 *
 * No Windows header in here: this file builds on the host so that the
 * parsing and the report are tested where tests are cheap, and again
 * inside the Windows .exe so that `aurbridge selftest` runs the same
 * cases on a real PC. */

#include "hwinv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ── tiny helpers, ASCII only ────────────────────────────────────── */
static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int ci_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (lc((unsigned char)*a) != lc((unsigned char)*b)) return 0;
    return *a == *b;
}

static int ci_prefix(const char *s, const char *pre)
{
    for (; *pre; s++, pre++)
        if (!*s || lc((unsigned char)*s) != lc((unsigned char)*pre)) return 0;
    return 1;
}

static int ci_has(const char *hay, const char *needle)
{
    if (!*needle) return 1;
    for (; *hay; hay++) if (ci_prefix(hay, needle)) return 1;
    return 0;
}

static void cpy(char *dst, size_t n, const char *src)
{
    if (!n) return;
    snprintf(dst, n, "%s", src ? src : "");
}

static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = lc(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* "TOKEN" followed by exactly `digits` hex digits (and then not another
 * hex digit). Returns the value, or -1. Searches the whole string. */
static long find_hex(const char *s, const char *tok, int digits)
{
    size_t tl = strlen(tok);
    for (; *s; s++) {
        if (!ci_prefix(s, tok)) continue;
        /* the token has to start a field: at the start, or after \ or & */
        const char *p = s + tl;
        long v = 0; int n = 0;
        while (n < digits && hexv((unsigned char)p[n]) >= 0) { v = v * 16 + hexv((unsigned char)p[n]); n++; }
        if (n != digits) continue;
        if (hexv((unsigned char)p[n]) >= 0) continue;
        return v;
    }
    return -1;
}

/* ── hardware ids ────────────────────────────────────────────────── */
int hwinv_parse_cc(const char *compat)
{
    if (!compat) return -1;
    long c6 = find_hex(compat, "CC_", 6);
    if (c6 >= 0) return (int)c6;
    long c4 = find_hex(compat, "CC_", 4);          /* PCI\...&CC_0403: no programming interface */
    if (c4 >= 0) return (int)(c4 << 8);
    long cl = find_hex(compat, "Class_", 2), sc = find_hex(compat, "SubClass_", 2), pr = find_hex(compat, "Prot_", 2);
    if (cl >= 0) return (int)((cl << 16) | ((sc >= 0 ? sc : 0) << 8) | (pr >= 0 ? pr : 0));
    return -1;
}

int hwinv_parse_hwid(const char *hwid, hw_id *o)
{
    memset(o, 0, sizeof *o);
    o->rev = -1; o->mi = -1; o->cc = -1;
    if (!hwid) return 0;

    if (ci_prefix(hwid, "PCI\\")) {
        long ven = find_hex(hwid, "VEN_", 4), dev = find_hex(hwid, "DEV_", 4);
        o->bus = HW_BUS_PCI;
        if (ven >= 0 && dev >= 0) { o->has_ids = 1; o->vid = (uint16_t)ven; o->did = (uint16_t)dev; }
        long ss = find_hex(hwid, "SUBSYS_", 8); if (ss >= 0) o->subsys = (uint32_t)ss;
        long rv = find_hex(hwid, "REV_", 2);    if (rv >= 0) o->rev = (int)rv;
        return 1;
    }
    if (ci_prefix(hwid, "USB\\")) {
        long v = find_hex(hwid, "VID_", 4), p = find_hex(hwid, "PID_", 4);
        o->bus = HW_BUS_USB;
        if (v >= 0 && p >= 0) { o->has_ids = 1; o->vid = (uint16_t)v; o->did = (uint16_t)p; }
        long mi = find_hex(hwid, "MI_", 2); if (mi >= 0) o->mi = (int)mi;
        long rv = find_hex(hwid, "REV_", 4); if (rv >= 0) o->rev = (int)rv;
        return 1;
    }
    if (ci_prefix(hwid, "HDAUDIO\\")) {
        long v = find_hex(hwid, "VEN_", 4), d = find_hex(hwid, "DEV_", 4);
        o->bus = HW_BUS_HDA;
        if (v >= 0 && d >= 0) { o->has_ids = 1; o->vid = (uint16_t)v; o->did = (uint16_t)d; }
        long ss = find_hex(hwid, "SUBSYS_", 8); if (ss >= 0) o->subsys = (uint32_t)ss;
        long rv = find_hex(hwid, "REV_", 4);    if (rv >= 0) o->rev = (int)rv;
        return 1;
    }
    if (ci_prefix(hwid, "ACPI\\")) {
        o->bus = HW_BUS_ACPI;
        const char *p = hwid + 5; size_t n = 0;
        while (p[n] && p[n] != '\\' && n < sizeof o->acpi - 1) { o->acpi[n] = p[n]; n++; }
        o->acpi[n] = 0;
        return 1;
    }
    if (ci_prefix(hwid, "USBPRINT\\")) {
        cpy(o->ieee, sizeof o->ieee, hwid + 9);
        o->bus = HW_BUS_USB;
        return 1;
    }
    return 0;
}

/* ── what kind of thing ──────────────────────────────────────────── */
hw_kind hwinv_kind(const char *cls, const hw_id *id, const char *name,
                   const char *service, int if_type)
{
    if (!cls) cls = "";
    if (!name) name = "";
    if (!service) service = "";
    int real = id->bus == HW_BUS_PCI || id->bus == HW_BUS_USB ||
               id->bus == HW_BUS_HDA || id->bus == HW_BUS_ACPI;
    if (!real) return HW_K_OTHER;           /* ROOT\, SWD\, BTH\, VMBUS\ ... */

    if (ci_eq(cls, "Net")) {
        if (if_type == 71) return HW_K_WIFI;                 /* IEEE 802.11  */
        if (if_type == 6)  return HW_K_ETHERNET;
        if (ci_has(name, "wireless") || ci_has(name, "wi-fi") || ci_has(name, "wifi") ||
            ci_has(name, "wlan") || ci_has(name, "802.11") || ci_has(name, "wi fi"))
            return HW_K_WIFI;
        /* A PCI/USB network device that is neither: a wired card. A
         * mobile-broadband modem is not one, but nothing here promises
         * more than "a network device" for it. */
        return HW_K_ETHERNET;
    }
    if (ci_eq(cls, "Bluetooth")) return HW_K_BLUETOOTH;
    if (ci_eq(cls, "MEDIA"))     return HW_K_AUDIO;
    if (ci_eq(cls, "Camera"))    return HW_K_CAMERA;
    if (ci_eq(cls, "Image")) {
        if (ci_has(name, "camera") || ci_has(name, "webcam") || ci_has(name, "video") ||
            ci_has(name, " cam"))
            return HW_K_CAMERA;
        return HW_K_SCANNER;
    }
    if (ci_eq(cls, "Display"))   return HW_K_GPU;
    if (ci_eq(cls, "Biometric")) return HW_K_FINGERPRINT;
    if (id->bus == HW_BUS_PCI &&
        (ci_eq(cls, "SCSIAdapter") || ci_eq(cls, "HDC") || ci_eq(cls, "NVMe") ||
         ci_eq(service, "stornvme") || ci_eq(service, "storahci") ||
         ci_eq(service, "iaStorAC") || ci_eq(service, "iaStorAVC") ||
         ci_eq(service, "iaStorA") || ci_eq(service, "iaStorVD") ||
         ci_eq(service, "iaStorV") || ci_eq(service, "amdsbs") ||
         ci_eq(service, "amdxata")))
        return HW_K_STORAGE;
    return HW_K_OTHER;
}

static const char *const KEY[HW_K_COUNT]  = { "other", "wifi", "ethernet", "bluetooth",
    "audio", "camera", "gpu", "storage", "scanner", "fingerprint" };
static const char *const WORD[HW_K_COUNT] = { "Other", "Wi-Fi", "Network cable", "Bluetooth",
    "Sound", "Camera", "Screen chip", "Drive controller", "Scanner", "Fingerprint reader" };

const char *hwinv_kind_key(hw_kind k)  { return k >= 0 && k < HW_K_COUNT ? KEY[k]  : "other"; }
const char *hwinv_kind_word(hw_kind k) { return k >= 0 && k < HW_K_COUNT ? WORD[k] : "Other"; }
const char *hwinv_state_key(hw_vstate s)
{
    switch (s) {
        case HW_V_WORKS:   return "works";
        case HW_V_PARTIAL: return "partial";
        case HW_V_WONT:    return "wont";
        case HW_V_UNKNOWN: return "unknown";
        default:           return "none";
    }
}

/* ── the record ──────────────────────────────────────────────────── */
hw_inv *hwinv_new(void)
{
    hw_inv *v = calloc(1, sizeof *v);
    if (!v) return NULL;
    v->devs = calloc(HW_MAX_DEVS, sizeof *v->devs);
    if (!v->devs) { free(v); return NULL; }
    v->is_uefi = v->secure_boot = v->has_battery = -1;
    v->sys_disk_kind = v->sys_disk_bus = -1;
    v->bitlocker = v->bitlocker_any = -1;
    v->pf_go = -1;
    return v;
}

void hwinv_free(hw_inv *v)
{
    if (!v) return;
    free(v->devs);
    free(v);
}

int hwinv_add_dev(hw_inv *v, const char *cls, const char *hwid, const char *compat,
                  const char *name, const char *mfg, const char *service, int problem, int if_type)
{
    hw_id id;
    hwinv_parse_hwid(hwid, &id);
    id.cc = hwinv_parse_cc(compat);
    hw_kind k = hwinv_kind(cls, &id, name, service, if_type);
    /* Kept though she would not recognise it: every PCI device (an
     * unknown camera chip is usually "System" or "Other" to Windows),
     * every whole USB device with ids (not its interfaces, and not a
     * hub), and anything Windows itself had a problem with. The matcher
     * looks at all of them by id; the report lists the odd ones. */
    int keep = k != HW_K_OTHER || (id.bus == HW_BUS_PCI && id.has_ids) ||
               (id.bus == HW_BUS_USB && id.has_ids && id.mi < 0 && !ci_has(name ? name : "", "hub")) ||
               (problem && id.bus != HW_BUS_OTHER);
    if (!keep) { v->n_dropped++; return -1; }
    if (v->n_devs >= HW_MAX_DEVS) { v->n_dropped++; return -1; }
    hw_dev *d = &v->devs[v->n_devs];
    memset(d, 0, sizeof *d);
    d->kind = k; d->bus = id.bus; d->has_ids = id.has_ids;
    d->vid = id.vid; d->did = id.did; d->subsys = id.subsys; d->rev = id.rev; d->cc = id.cc;
    d->problem = problem;
    cpy(d->cls, sizeof d->cls, cls);
    cpy(d->name, sizeof d->name, name);
    cpy(d->mfg, sizeof d->mfg, mfg);
    cpy(d->service, sizeof d->service, service);
    cpy(d->hwid, sizeof d->hwid, hwid);
    return v->n_devs++;
}

/* A queue that prints to a file or to nobody is not a printer. */
static int printer_is_virtual(const char *name, const char *driver, const char *port)
{
    static const char *const ports[] = { "PORTPROMPT:", "FILE:", "nul:", "XPSPort:",
        "SHRFAX:", "Microsoft.Office", "TS0", "TS1", "TS2", "TS3", "TS4", "TS5", NULL };
    /* "xps" alone is not here: Lexmark and Xerox name their real printers'
     * XPS drivers that way. Microsoft's own is a "document writer". */
    static const char *const words[] = { "pdf", "fax", "onenote", "easy print",
        "snagit", "send to", "document writer", NULL };
    for (int i = 0; ports[i]; i++) if (ci_prefix(port, ports[i])) return 1;
    for (int i = 0; words[i]; i++)
        if (ci_has(name, words[i]) || ci_has(driver, words[i])) return 1;
    return 0;
}

static hw_conn port_conn(const char *port)
{
    if (ci_prefix(port, "USB") || ci_prefix(port, "DOT4")) return HW_CONN_USB;
    if (ci_prefix(port, "IP_") || ci_prefix(port, "WSD") || ci_prefix(port, "http") ||
        ci_prefix(port, "ipp") || ci_prefix(port, "BRN")) return HW_CONN_NETWORK;
    if (ci_prefix(port, "LPT") || ci_prefix(port, "COM") || ci_prefix(port, "\\\\"))
        return HW_CONN_OTHER;
    /* a bare address or host name: "192.168.1.20", "printer.local" */
    int dots = 0, other = 0;
    for (const char *p = port; *p; p++) {
        if (*p == '.') dots++;
        else if (*p < '0' || *p > '9') other++;
    }
    if (dots >= 1 && (other == 0 || dots >= 2)) return HW_CONN_NETWORK;
    return HW_CONN_UNKNOWN;
}

int hwinv_add_printer(hw_inv *v, const char *name, const char *driver,
                      const char *port, int is_default)
{
    if (!name) name = "";
    if (!driver) driver = "";
    if (!port) port = "";
    if (printer_is_virtual(name, driver, port)) { return -1; }
    if (v->n_printers >= HW_MAX_PRINTERS) return -1;
    hw_printer *p = &v->printers[v->n_printers];
    memset(p, 0, sizeof *p);
    cpy(p->name, sizeof p->name, name);
    cpy(p->driver, sizeof p->driver, driver);
    cpy(p->port, sizeof p->port, port);
    p->conn = port_conn(port);
    p->is_default = is_default ? 1 : 0;
    return v->n_printers++;
}

void hwinv_printer_usb(hw_inv *v, int i, uint16_t vid, uint16_t pid, const char *ieee)
{
    if (i < 0 || i >= v->n_printers) return;
    hw_printer *p = &v->printers[i];
    if (vid || pid) { p->has_usb = 1; p->vid = vid; p->pid = pid; p->conn = HW_CONN_USB; }
    if (ieee) cpy(p->ieee, sizeof p->ieee, ieee);
}

/* lower-case letters and digits only: "HP LaserJet Pro M404-M405" and
 * "HPLaserJet_Pro_M404dn" become comparable */
static size_t norm(const char *s, char *out, size_t n)
{
    size_t j = 0;
    for (; *s && j + 1 < n; s++) {
        int c = lc((unsigned char)*s);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[j++] = (char)c;
    }
    out[j] = 0;
    return j;
}

/* the longest run of characters the two strings have in common */
static size_t lcs_len(const char *a, const char *b)
{
    size_t best = 0, la = strlen(a), lb = strlen(b);
    for (size_t i = 0; i < la; i++)
        for (size_t j = 0; j < lb; j++) {
            size_t k = 0;
            while (i + k < la && j + k < lb && a[i + k] == b[j + k]) k++;
            if (k > best) best = k;
        }
    return best;
}

void hwinv_pair_usb_printers(hw_inv *v, hw_usbprn *u, int n)
{
    char qn[200], dn[200];
    /* the model without Windows' four-character checksum on the end */
    for (int d = 0; d < n; d++) u[d].used = 0;
    for (int i = 0; i < v->n_printers; i++) {
        hw_printer *p = &v->printers[i];
        if (p->conn != HW_CONN_USB || p->has_usb) continue;
        char both[200];
        snprintf(both, sizeof both, "%s %s", p->driver, p->name);
        size_t ql = norm(both, qn, sizeof qn);
        int best = -1; size_t bl = 0;
        for (int d = 0; d < n; d++) {
            if (u[d].used) continue;
            size_t dl = norm(u[d].ieee, dn, sizeof dn);
            if (dl > 8) dn[dl - 4] = 0, dl -= 4;
            size_t m = lcs_len(qn, dn), shorter = ql < dl ? ql : dl;
            if (m >= 6 && m * 100 >= 60 * shorter && m > bl) { best = d; bl = m; }
        }
        if (best >= 0) {
            hwinv_printer_usb(v, i, u[best].vid, u[best].pid, u[best].ieee);
            u[best].used = 1;
        }
    }
    /* one queue left over and one device left over: they go together */
    int lq = -1, nq = 0, ld = -1, nd = 0;
    for (int i = 0; i < v->n_printers; i++)
        if (v->printers[i].conn == HW_CONN_USB && !v->printers[i].has_usb) { lq = i; nq++; }
    for (int d = 0; d < n; d++) if (!u[d].used) { ld = d; nd++; }
    if (nq == 1 && nd == 1) {
        hwinv_printer_usb(v, lq, u[ld].vid, u[ld].pid, u[ld].ieee);
        u[ld].used = 1;
    }
    for (int d = 0; d < n; d++) {
        if (u[d].used) continue;
        int k = hwinv_add_printer(v, u[d].ieee, "(not set up in Windows)", "USB", 0);
        if (k >= 0) hwinv_printer_usb(v, k, u[d].vid, u[d].pid, u[d].ieee);
    }
}

/* ── a string builder, so the report is built once and written once ── */
typedef struct { char *s; size_t n, cap; int oom; } sb;

static void sb_put(sb *b, const char *fmt, ...)
{
    if (b->oom) return;
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int w = vsnprintf(b->s ? b->s + b->n : NULL, b->cap - b->n, fmt, ap);
        va_end(ap);
        if (w < 0) { b->oom = 1; return; }
        if ((size_t)w < b->cap - b->n) { b->n += (size_t)w; return; }
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc - b->n <= (size_t)w) nc *= 2;
        char *t = realloc(b->s, nc);
        if (!t) { b->oom = 1; return; }
        b->s = t; b->cap = nc;
    }
}

static void sb_json(sb *b, const char *s)
{
    sb_put(b, "\"");
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        if (*p == '"' || *p == '\\') sb_put(b, "\\%c", *p);
        else if (*p < 0x20) sb_put(b, "\\u%04x", *p);
        else sb_put(b, "%c", *p);
    }
    sb_put(b, "\"");
}

static char *sb_done(sb *b)
{
    if (b->oom) { free(b->s); return NULL; }
    if (!b->s) { b->s = malloc(1); if (b->s) b->s[0] = 0; }
    return b->s;
}

static void size_words(uint64_t bytes, char *out, size_t n)
{
    double v = (double)bytes;
    if (bytes == 0) { cpy(out, n, "unknown"); return; }
    if (v >= 1099511627776.0) snprintf(out, n, "%.1f TB", v / 1099511627776.0);
    else if (v >= 1073741824.0) snprintf(out, n, "%.1f GB", v / 1073741824.0);
    else snprintf(out, n, "%.0f MB", v / 1048576.0);
}

static void id_words(const hw_dev *d, char *out, size_t n)
{
    if (!d->has_ids) { cpy(out, n, "-"); return; }
    snprintf(out, n, "%s %04X:%04X", d->bus == HW_BUS_PCI ? "PCI" : d->bus == HW_BUS_USB ? "USB" :
             d->bus == HW_BUS_HDA ? "HDA" : "ID", d->vid, d->did);
}

static const char *yn3(int v) { return v == 1 ? "yes" : v == 0 ? "no" : "unknown"; }

/* ── the plain report ─────────────────────────────────────────────── */
char *hwinv_text(const hw_inv *v, const char *tool, const char *made)
{
    sb b = { 0 };
    char t1[48], t2[48];
    sb_put(&b, "AurOS - what is inside this PC\r\n");
    sb_put(&b, "==============================\r\n");
    sb_put(&b, "Made %s by %s.\r\n", made ? made : "(no clock)", tool ? tool : "AurOS Setup");
    sb_put(&b, "Nothing on this PC was changed. This file holds no serial numbers, no PC name,\r\n"
               "no user name, no Wi-Fi name and no file names.\r\n\r\n");

    sb_put(&b, "THIS PC\r\n");
    sb_put(&b, "  Maker and model   %s %s\r\n", v->dmi_vendor[0] ? v->dmi_vendor : "(not said)",
           v->dmi_model);
    if (v->dmi_family[0]) sb_put(&b, "  Family            %s\r\n", v->dmi_family);
    sb_put(&b, "  Firmware          %s %s %s\r\n", v->bios_vendor, v->bios_version, v->bios_date);
    sb_put(&b, "  Processor         %s (%d threads)\r\n", v->cpu[0] ? v->cpu : "unknown", v->cpu_threads);
    size_words(v->ram_bytes, t1, sizeof t1);
    sb_put(&b, "  Memory            %s\r\n", t1);
    sb_put(&b, "  Windows           %s%s%s (build %d)%s\r\n", v->os_name, v->os_display[0] ? " " : "",
           v->os_display, v->os_build, v->is_arm ? "  ON AN ARM PROCESSOR" : "");
    sb_put(&b, "  Starts up with    %s, Secure Boot %s\r\n",
           v->is_uefi == 1 ? "UEFI" : v->is_uefi == 0 ? "old BIOS" : "unknown firmware",
           v->secure_boot == 1 ? "on" : v->secure_boot == 0 ? "off" : "unknown");
    sb_put(&b, "  Battery           %s\r\n",
           v->has_battery == 1 ? "yes (a laptop or tablet)" : v->has_battery == 0 ? "no" : "unknown");
    size_words(v->sys_disk_bytes, t1, sizeof t1);
    size_words(v->sys_free_bytes, t2, sizeof t2);
    sb_put(&b, "  Drive Windows is on  %s, %s, %s free\r\n", t1,
           v->sys_disk_kind == 1 ? "solid state (fast)" : v->sys_disk_kind == 0 ? "hard disk (slow)" : "kind unknown", t2);
    if (v->sys_disk_model[0]) sb_put(&b, "                       %s\r\n", v->sys_disk_model);
    sb_put(&b, "  BitLocker         Windows drive encrypted: %s;  any drive encrypted: %s\r\n",
           yn3(v->bitlocker), yn3(v->bitlocker_any));

    sb_put(&b, "\r\nWOULD THE AurOS INSTALLER ACCEPT THIS PC?\r\n");
    if (v->pf_go < 0) sb_put(&b, "  (its own check was not run)\r\n");
    else {
        sb_put(&b, "  %s  (%d reasons to stop, %d things to know)\r\n",
               v->pf_go ? "Yes." : "NO - it would stop.", v->pf_blocks, v->pf_warns);
        for (int i = 0; i < v->pf_n_find; i++)
            sb_put(&b, "  [%s] %s  (%s)\r\n", v->pf_find[i].sev, v->pf_find[i].title, v->pf_find[i].id);
    }

    sb_put(&b, "\r\nWHAT WILL WORK (the sentences she would be shown)\r\n");
    if (v->n_verdicts == 0) sb_put(&b, "  (not judged by this version)\r\n");
    for (int i = 0; i < v->n_verdicts; i++) {
        const hw_verdict *x = &v->verdicts[i];
        if (x->state == HW_V_NONE) continue;
        sb_put(&b, "  %s\r\n", x->says);
        if (x->why[0]) sb_put(&b, "      (%s: %s)\r\n", x->id, x->why);
    }

    sb_put(&b, "\r\nDEVICES WINDOWS SEES (%d kept, %d skipped as plumbing)\r\n", v->n_devs, v->n_dropped);
    for (int k = 1; k < HW_K_COUNT; k++) {
        for (int i = 0; i < v->n_devs; i++) {
            const hw_dev *d = &v->devs[i];
            if ((int)d->kind != k) continue;
            id_words(d, t1, sizeof t1);
            sb_put(&b, "  %-18s %-14s %s%s%s", hwinv_kind_word(d->kind), t1, d->name[0] ? d->name : "(no name)",
                   d->in_use ? "  [Windows uses this now]" : "",
                   d->problem ? "  [Windows has a problem with it]" : "");
            if (d->subsys) sb_put(&b, "  sub %04X:%04X", (unsigned)(d->subsys & 0xFFFF), (unsigned)(d->subsys >> 16));
            sb_put(&b, "\r\n");
        }
    }
    int h0 = 0, h1 = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < v->n_devs; i++) {
            const hw_dev *d = &v->devs[i];
            if (d->kind != HW_K_OTHER || (pass == 0) != (d->problem != 0)) continue;
            if (pass == 0 && !h0++) sb_put(&b, "  Windows has NO driver for these:\r\n");
            if (pass == 1 && !h1++) sb_put(&b, "  Everything else on the PCI and USB buses:\r\n");
            id_words(d, t1, sizeof t1);
            sb_put(&b, "  %-18s %-14s %s\r\n", pass == 0 ? "No driver" : "", t1,
                   d->name[0] ? d->name : d->hwid);
        }
    }

    sb_put(&b, "\r\nPRINTERS (the names are the ones set up in Windows: they may be somebody's own words)\r\n");
    if (!v->n_printers) sb_put(&b, "  none set up in Windows\r\n");
    for (int i = 0; i < v->n_printers; i++) {
        const hw_printer *p = &v->printers[i];
        sb_put(&b, "  %s%s\r\n      driver: %s\r\n      port: %s (%s)", p->name, p->is_default ? "  [default]" : "",
               p->driver, p->port,
               p->conn == HW_CONN_USB ? "USB" : p->conn == HW_CONN_NETWORK ? "network" :
               p->conn == HW_CONN_OTHER ? "other" : "kind unknown");
        if (p->has_usb) sb_put(&b, "  USB %04X:%04X", p->vid, p->pid);
        if (p->ieee[0]) sb_put(&b, "\r\n      id: %s", p->ieee);
        sb_put(&b, "\r\n");
    }
    return sb_done(&b);
}

/* ── the same, for machines ───────────────────────────────────────── */
char *hwinv_json(const hw_inv *v, const char *tool, const char *made)
{
    sb b = { 0 };
    sb_put(&b, "{\n  \"schema\": 1,\n  \"tool\": "); sb_json(&b, tool);
    sb_put(&b, ",\n  \"made\": "); sb_json(&b, made);
    sb_put(&b, ",\n  \"machine\": {\n    \"vendor\": "); sb_json(&b, v->dmi_vendor);
    sb_put(&b, ", \"model\": "); sb_json(&b, v->dmi_model);
    sb_put(&b, ", \"family\": "); sb_json(&b, v->dmi_family);
    sb_put(&b, ", \"board\": "); sb_json(&b, v->dmi_board);
    sb_put(&b, ",\n    \"bios_vendor\": "); sb_json(&b, v->bios_vendor);
    sb_put(&b, ", \"bios_version\": "); sb_json(&b, v->bios_version);
    sb_put(&b, ", \"bios_date\": "); sb_json(&b, v->bios_date);
    sb_put(&b, ",\n    \"cpu\": "); sb_json(&b, v->cpu);
    sb_put(&b, ", \"threads\": %d, \"ram_bytes\": %llu,\n", v->cpu_threads, (unsigned long long)v->ram_bytes);
    sb_put(&b, "    \"uefi\": %d, \"secure_boot\": %d, \"battery\": %d, \"arm\": %d\n  },\n",
           v->is_uefi, v->secure_boot, v->has_battery, v->is_arm);
    sb_put(&b, "  \"windows\": { \"name\": "); sb_json(&b, v->os_name);
    sb_put(&b, ", \"display\": "); sb_json(&b, v->os_display);
    sb_put(&b, ", \"build\": %d },\n", v->os_build);
    sb_put(&b, "  \"system_drive\": { \"model\": "); sb_json(&b, v->sys_disk_model);
    sb_put(&b, ", \"bytes\": %llu, \"free_bytes\": %llu, \"kind\": %d, \"bus\": %d, "
               "\"bitlocker\": %d, \"bitlocker_any\": %d },\n",
           (unsigned long long)v->sys_disk_bytes, (unsigned long long)v->sys_free_bytes,
           v->sys_disk_kind, v->sys_disk_bus, v->bitlocker, v->bitlocker_any);

    sb_put(&b, "  \"installer\": { \"checked\": %s, \"go\": %s, \"blocks\": %d, \"warnings\": %d, \"findings\": [",
           v->pf_go >= 0 ? "true" : "false", v->pf_go == 1 ? "true" : "false", v->pf_blocks, v->pf_warns);
    for (int i = 0; i < v->pf_n_find; i++) {
        sb_put(&b, "%s{\"id\": ", i ? ", " : ""); sb_json(&b, v->pf_find[i].id);
        sb_put(&b, ", \"severity\": "); sb_json(&b, v->pf_find[i].sev);
        sb_put(&b, ", \"title\": "); sb_json(&b, v->pf_find[i].title); sb_put(&b, "}");
    }
    sb_put(&b, "] },\n");

    sb_put(&b, "  \"verdicts\": [");
    for (int i = 0; i < v->n_verdicts; i++) {
        const hw_verdict *x = &v->verdicts[i];
        sb_put(&b, "%s\n    {\"kind\": \"%s\", \"state\": \"%s\", \"blocks\": %s, \"device\": %d, \"printer\": %d, \"id\": ",
               i ? "," : "", x->printer >= 0 ? "printer" : hwinv_kind_key(x->kind), hwinv_state_key(x->state),
               x->blocks ? "true" : "false", x->dev, x->printer);
        sb_json(&b, x->id); sb_put(&b, ", \"link\": "); sb_json(&b, x->link);
        sb_put(&b, ", \"says\": "); sb_json(&b, x->says);
        sb_put(&b, ", \"why\": "); sb_json(&b, x->why); sb_put(&b, "}");
    }
    sb_put(&b, "%s],\n", v->n_verdicts ? "\n  " : "");

    sb_put(&b, "  \"devices\": [");
    for (int i = 0; i < v->n_devs; i++) {
        const hw_dev *d = &v->devs[i];
        sb_put(&b, "%s\n    {\"kind\": \"%s\", \"bus\": \"%s\", ", i ? "," : "", hwinv_kind_key(d->kind),
               d->bus == HW_BUS_PCI ? "pci" : d->bus == HW_BUS_USB ? "usb" : d->bus == HW_BUS_HDA ? "hda" :
               d->bus == HW_BUS_ACPI ? "acpi" : "other");
        if (d->has_ids) sb_put(&b, "\"vid\": \"%04x\", \"did\": \"%04x\", ", d->vid, d->did);
        if (d->subsys) sb_put(&b, "\"subsys\": \"%08x\", ", (unsigned)d->subsys);
        sb_put(&b, "\"problem\": %d, \"in_use\": %s, \"class\": ", d->problem, d->in_use ? "true" : "false");
        sb_json(&b, d->cls); sb_put(&b, ", \"name\": "); sb_json(&b, d->name);
        sb_put(&b, ", \"mfg\": "); sb_json(&b, d->mfg);
        sb_put(&b, ", \"service\": "); sb_json(&b, d->service);
        sb_put(&b, ", \"hwid\": "); sb_json(&b, d->hwid); sb_put(&b, "}");
    }
    sb_put(&b, "%s],\n  \"devices_skipped\": %d,\n", v->n_devs ? "\n  " : "", v->n_dropped);

    sb_put(&b, "  \"printers\": [");
    for (int i = 0; i < v->n_printers; i++) {
        const hw_printer *p = &v->printers[i];
        sb_put(&b, "%s\n    {\"name\": ", i ? "," : ""); sb_json(&b, p->name);
        sb_put(&b, ", \"driver\": "); sb_json(&b, p->driver);
        sb_put(&b, ", \"port\": "); sb_json(&b, p->port);
        sb_put(&b, ", \"connection\": \"%s\", \"default\": %s", p->conn == HW_CONN_USB ? "usb" :
               p->conn == HW_CONN_NETWORK ? "network" : p->conn == HW_CONN_OTHER ? "other" : "unknown",
               p->is_default ? "true" : "false");
        if (p->has_usb) sb_put(&b, ", \"vid\": \"%04x\", \"pid\": \"%04x\"", p->vid, p->pid);
        sb_put(&b, ", \"ieee\": "); sb_json(&b, p->ieee); sb_put(&b, "}");
    }
    sb_put(&b, "%s]\n}\n", v->n_printers ? "\n  " : "");
    return sb_done(&b);
}

/* ── the line the journal carries ─────────────────────────────────── */
size_t hwinv_journal(const hw_inv *v, char *out, size_t n)
{
    static const struct { const char *key; hw_kind kind; } K[] = {
        { "wifi", HW_K_WIFI }, { "eth", HW_K_ETHERNET }, { "bt", HW_K_BLUETOOTH }, { "audio", HW_K_AUDIO },
        { "cam", HW_K_CAMERA }, { "gpu", HW_K_GPU }, { "scan", HW_K_SCANNER }, { "fp", HW_K_FINGERPRINT } };
    if (!n) return 0;
    out[0] = 0;
    size_t w = (size_t)snprintf(out, n, "hw1");
    for (size_t k = 0; k < sizeof K / sizeof K[0] && w < n; k++) {
        const char *st = "none";
        for (int i = 0; i < v->n_verdicts; i++)
            if (v->verdicts[i].kind == K[k].kind && v->verdicts[i].printer < 0) st = hwinv_state_key(v->verdicts[i].state);
        w += (size_t)snprintf(out + w, n - w, ";%s=%s", K[k].key, st);
    }
    int prn = 0, bad = 0, blk = 0;
    for (int i = 0; i < v->n_verdicts; i++) {
        if (v->verdicts[i].printer >= 0) { prn++; if (v->verdicts[i].state == HW_V_WONT) bad++; }
        if (v->verdicts[i].blocks) blk = 1;
    }
    if (w < n) w += (size_t)snprintf(out + w, n - w, ";prn=%d;prnbad=%d;blk=%d", prn, bad, blk);
    if (w >= n) { out[n - 1] = 0; w = n - 1; }
    return w;
}

/* ═══════════════════════════════════════════════════════════════════
 *  selftest: real hardware-ID strings (the shapes Windows really
 *  produces), what they must be called, and that the report is JSON.
 * ═══════════════════════════════════════════════════════════════════ */
static int g_bad;
static void t(const char *name, int ok)
{
    printf("  %-62s %s\n", name, ok ? "ok" : "WRONG");
    if (!ok) g_bad++;
}

int hwinv_selftest(void)
{
    g_bad = 0;
    hw_id id;

    /* hardware ids, as Windows writes them */
    t("PCI: Broadcom BCM4352 (14e4:43b1) parses",
      hwinv_parse_hwid("PCI\\VEN_14E4&DEV_43B1&SUBSYS_0608105B&REV_03", &id) && id.bus == HW_BUS_PCI &&
      id.vid == 0x14E4 && id.did == 0x43B1 && id.subsys == 0x0608105B && id.rev == 3);
    t("PCI: lower case and no subsystem",
      hwinv_parse_hwid("pci\\ven_8086&dev_06f0", &id) && id.vid == 0x8086 && id.did == 0x06F0 && id.rev == -1);
    t("PCI: a five-digit device is not a device (no guessing)",
      hwinv_parse_hwid("PCI\\VEN_8086&DEV_06F00", &id) && !id.has_ids);
    t("USB: a composite interface keeps its interface number",
      hwinv_parse_hwid("USB\\VID_0BDA&PID_5530&MI_00", &id) && id.bus == HW_BUS_USB &&
      id.vid == 0x0BDA && id.did == 0x5530 && id.mi == 0);
    t("USB: a plain device has no interface number",
      hwinv_parse_hwid("USB\\VID_27C6&PID_55B4&REV_0100", &id) && id.mi == -1 && id.rev == 0x100);
    t("HDA: a codec function group reads vendor and device",
      hwinv_parse_hwid("HDAUDIO\\FUNC_01&VEN_10EC&DEV_0236&SUBSYS_17AA2292&REV_1000", &id) &&
      id.bus == HW_BUS_HDA && id.vid == 0x10EC && id.did == 0x0236);
    t("ACPI: the name is kept",
      hwinv_parse_hwid("ACPI\\GDIX0000", &id) && id.bus == HW_BUS_ACPI && !strcmp(id.acpi, "GDIX0000"));
    t("USBPRINT: the printer's own id is kept",
      hwinv_parse_hwid("USBPRINT\\HPLaserJet_Pro_M404dnC7A0", &id) && !strcmp(id.ieee, "HPLaserJet_Pro_M404dnC7A0"));
    t("class code: PCI audio", hwinv_parse_cc("PCI\\VEN_8086&DEV_51C8&CC_040300;PCI\\VEN_8086&DEV_51C8&CC_0403") == 0x040300);
    t("class code: only the short PCI form", hwinv_parse_cc("PCI\\VEN_8086&DEV_51C8&CC_0403") == 0x040300);
    t("class code: USB video interface", hwinv_parse_cc("USB\\Class_0E&SubClass_01&Prot_00;USB\\Class_0E&SubClass_01;USB\\Class_0E") == 0x0E0100);
    t("class code: USB Bluetooth radio", hwinv_parse_cc("USB\\Class_E0&SubClass_01&Prot_01") == 0xE00101);
    t("class code: nothing to read", hwinv_parse_cc("USB\\COMPOSITE") == -1 && hwinv_parse_cc(NULL) == -1);
    t("garbage, empty and NULL are not a bus",
      !hwinv_parse_hwid("ROOT\\SYSTEM\\0001", &id) && !hwinv_parse_hwid("", &id) && !hwinv_parse_hwid(NULL, &id));

    /* kinds */
    hwinv_parse_hwid("PCI\\VEN_8086&DEV_06F0", &id);
    t("Net by interface type 71 is Wi-Fi", hwinv_kind("Net", &id, "Intel(R) Ethernet", "", 71) == HW_K_WIFI);
    t("Net by name is Wi-Fi when the type is unknown",
      hwinv_kind("Net", &id, "Intel(R) Wi-Fi 6 AX201 160MHz", "Netwtw10", 0) == HW_K_WIFI);
    t("Net with a plain name is a cable", hwinv_kind("Net", &id, "Realtek PCIe GbE Family Controller", "rt640x64", 0) == HW_K_ETHERNET);
    t("a ROOT-enumerated adapter is not hardware",
      (hwinv_parse_hwid("ROOT\\NET\\0000", &id), hwinv_kind("Net", &id, "TAP-Windows Adapter V9", "tap0901", 0)) == HW_K_OTHER);
    hwinv_parse_hwid("USB\\VID_0BDA&PID_5530&MI_00", &id);
    t("Image + webcam name is a camera", hwinv_kind("Image", &id, "Integrated Webcam", "", 0) == HW_K_CAMERA);
    t("Image + scanner name is a scanner", hwinv_kind("Image", &id, "EPSON Perfection V19", "", 0) == HW_K_SCANNER);
    t("Camera class is a camera", hwinv_kind("Camera", &id, "USB Video Device", "usbvideo", 0) == HW_K_CAMERA);
    t("Biometric is a fingerprint reader", hwinv_kind("Biometric", &id, "Goodix fingerprint", "", 0) == HW_K_FINGERPRINT);
    hwinv_parse_hwid("PCI\\VEN_8086&DEV_A0C8", &id);
    t("MEDIA on PCI is sound", hwinv_kind("MEDIA", &id, "Intel Smart Sound", "IntcAudioBus", 0) == HW_K_AUDIO);
    t("Display is a screen chip", hwinv_kind("Display", &id, "Intel(R) UHD Graphics", "igfx", 0) == HW_K_GPU);
    t("an NVMe controller on stornvme is storage",
      hwinv_kind("SCSIAdapter", &id, "Standard NVM Express Controller", "stornvme", 0) == HW_K_STORAGE);
    t("a PCI bridge is nothing we list", hwinv_kind("System", &id, "PCI Express Root Port", "pci", 0) == HW_K_OTHER);

    /* the record */
    hw_inv *v = hwinv_new();
    t("an inventory can be made", v != NULL);
    if (!v) return g_bad + 1;
    t("kept: a Wi-Fi card",
      hwinv_add_dev(v, "Net", "PCI\\VEN_14E4&DEV_43B1&SUBSYS_0608105B&REV_03",
                    "PCI\\VEN_14E4&DEV_43B1&CC_028000;PCI\\VEN_14E4&DEV_43B1&CC_0280",
                    "Broadcom 802.11ac Network Adapter", "Broadcom", "b57nd60a", 0, 71) == 0 &&
      v->devs[0].kind == HW_K_WIFI && v->devs[0].cc == 0x028000);
    t("kept, though she would not know it: a PCI bridge (the matcher looks at every id)",
      hwinv_add_dev(v, "System", "PCI\\VEN_8086&DEV_9D10", "PCI\\VEN_8086&DEV_9D10&CC_060400", "PCI Express Root Port", "", "pci", 0, 0) == 1 &&
      v->devs[1].kind == HW_K_OTHER);
    t("kept though unknown, when Windows had a problem with it",
      hwinv_add_dev(v, "Unknown", "PCI\\VEN_10EC&DEV_8852", "", "PCI Device", "", "", 28, 0) == 2 &&
      v->devs[2].kind == HW_K_OTHER && v->devs[2].problem == 28);
    t("dropped: a virtual adapter even with a problem (no real bus)",
      hwinv_add_dev(v, "Net", "ROOT\\NET\\0003", "", "Microsoft Wi-Fi Direct Virtual Adapter", "", "", 31, 71) == -1);
    t("dropped: a USB hub", hwinv_add_dev(v, "USB", "USB\\VID_8087&PID_0024", "", "Generic USB Hub", "", "usbhub", 0, 0) == -1);
    t("dropped: one USB interface of a camera (its parent is kept)",
      hwinv_add_dev(v, "USB", "USB\\VID_0BDA&PID_5530&MI_02", "", "USB Input Device", "", "HidUsb", 0, 0) == -1);
    t("dropped devices are counted", v->n_dropped == 3);

    int p0 = hwinv_add_printer(v, "Brother HL-L2350DW series", "Brother HL-L2350DW series", "USB001", 1);
    t("a USB printer is kept as USB", p0 == 0 && v->printers[0].conn == HW_CONN_USB);
    hwinv_printer_usb(v, p0, 0x04F9, 0x0054, "BrotherHL-L2350DW_seriesB3F4");
    t("and carries its USB id", v->printers[0].has_usb && v->printers[0].vid == 0x04F9);
    t("a network printer is network",
      hwinv_add_printer(v, "HP OfficeJet", "HP OfficeJet Pro 9015 series", "WSD-1a2b3c", 0) == 1 && v->printers[1].conn == HW_CONN_NETWORK);
    t("an address port is network",
      hwinv_add_printer(v, "Office", "Canon LBP6030", "192.168.1.40", 0) == 2 && v->printers[2].conn == HW_CONN_NETWORK);
    t("'Microsoft Print to PDF' is not a printer",
      hwinv_add_printer(v, "Microsoft Print to PDF", "Microsoft Print To PDF", "PORTPROMPT:", 0) == -1);
    t("a fax queue is not a printer", hwinv_add_printer(v, "Fax", "Microsoft Shared Fax Driver", "SHRFAX:", 0) == -1);
    t("a printer's fax queue is not a printer",
      hwinv_add_printer(v, "Brother MFC-L2750DW FAX", "Brother MFC-L2750DW series FAX", "USB003", 0) == -1);
    t("OneNote is not a printer", hwinv_add_printer(v, "OneNote (Desktop)", "Send to Microsoft OneNote 16 Driver", "nul:", 0) == -1);

    /* queue to device: by name, by "the only one", and the device with no queue */
    hw_inv *w = hwinv_new();
    hwinv_add_printer(w, "HP LaserJet Pro M404", "HP LaserJet Pro M404-M405 PCL-6 (V4)", "USB001", 1);
    hwinv_add_printer(w, "Brother HL-L2350DW series", "Brother HL-L2350DW series", "USB002", 0);
    hw_usbprn us[3];
    memset(us, 0, sizeof us);
    us[0].vid = 0x04F9; us[0].pid = 0x0054; strcpy(us[0].ieee, "BrotherHL-L2350DW_seriesB3F4");
    us[1].vid = 0x03F0; us[1].pid = 0x5011; strcpy(us[1].ieee, "HPLaserJet_Pro_M404dnC7A0");
    us[2].vid = 0x04B8; us[2].pid = 0x1109; strcpy(us[2].ieee, "EPSONXP-445_SeriesC8F1");
    hwinv_pair_usb_printers(w, us, 3);
    t("each queue gets the USB id of the printer with its model name",
      w->printers[0].vid == 0x03F0 && w->printers[0].pid == 0x5011 &&
      w->printers[1].vid == 0x04F9 && w->printers[1].pid == 0x0054);
    t("a printer Windows has no queue for is listed anyway",
      w->n_printers == 3 && w->printers[2].vid == 0x04B8 && w->printers[2].conn == HW_CONN_USB);
    hwinv_free(w);
    /* a weak resemblance is not a pairing: better no id than somebody else's */
    w = hwinv_new();
    hwinv_add_printer(w, "Generic Brother", "Generic Brother", "USB001", 1);
    memset(us, 0, sizeof us);
    us[0].vid = 0x04F9; us[0].pid = 0x0054; strcpy(us[0].ieee, "BrotherHL-L2350DW_seriesB3F4");
    us[1].vid = 0x04B8; us[1].pid = 0x1109; strcpy(us[1].ieee, "EPSONXP-445_SeriesC8F1");
    hwinv_pair_usb_printers(w, us, 2);
    t("a queue that only half-resembles a device is not given its id",
      !w->printers[0].has_usb && w->n_printers == 3);
    hwinv_free(w);
    w = hwinv_new();
    hwinv_add_printer(w, "Printer", "Generic / Text Only", "USB001", 1);
    memset(us, 0, sizeof us);
    us[0].vid = 0x04F9; us[0].pid = 0x0054; strcpy(us[0].ieee, "BrotherHL-L2350DW_seriesB3F4");
    hwinv_pair_usb_printers(w, us, 1);
    t("one queue and one device with unrelated names still go together",
      w->n_printers == 1 && w->printers[0].has_usb && w->printers[0].vid == 0x04F9);
    hwinv_free(w);

    strcpy(v->dmi_vendor, "ASUSTeK COMPUTER INC."); strcpy(v->dmi_model, "X555LA \"quoted\"");
    strcpy(v->os_name, "Windows 10 Home"); v->os_build = 19045; v->ram_bytes = 8ULL << 30;
    char *tx = hwinv_text(v, "AurOS Setup 0.1.0", "2026-10-23 12:00");
    char *js = hwinv_json(v, "AurOS Setup 0.1.0", "2026-10-23 12:00");
    t("the plain report says the Wi-Fi card and its ids",
      tx && strstr(tx, "Broadcom 802.11ac") && strstr(tx, "14E4:43B1"));
    t("the plain report says the printer and its port", tx && strstr(tx, "Brother HL-L2350DW") && strstr(tx, "USB 04F9:0054"));
    t("the plain report names the device Windows has no driver for", tx && strstr(tx, "NO driver") && strstr(tx, "8852"));
    t("the plain report lists the other PCI hardware, one line each", tx && strstr(tx, "PCI Express Root Port"));
    t("the JSON escapes quotes", js && strstr(js, "X555LA \\\"quoted\\\""));
    t("the JSON has the wifi device with its ids",
      js && strstr(js, "\"kind\": \"wifi\"") && strstr(js, "\"vid\": \"14e4\"") && strstr(js, "\"did\": \"43b1\""));
    free(tx); free(js);
    hwinv_free(v);
    return g_bad;
}
