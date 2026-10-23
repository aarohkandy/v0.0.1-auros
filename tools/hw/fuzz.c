/* fuzz.c -- random device lists and random names through the parser, the
 * matcher and both renderers, under the sanitizers when the compiler has
 * them (tools/hw/hwtest.sh). Device and printer names come from drivers
 * and from people; nothing they contain may crash the report. */
#include "hwcompat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long rs = 88172645463325252ULL;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 11); }

static const char *const TOK[] = { "PCI\\", "VEN_", "DEV_", "SUBSYS_", "REV_", "CC_", "USB\\", "VID_", "PID_", "MI_",
    "Class_", "SubClass_", "Prot_", "HDAUDIO\\", "FUNC_01&", "ACPI\\", "USBPRINT\\", "ROOT\\", "&", ";", "8086", "14E4",
    "43B1", "0BDA", "FFFF", "0", "A", "e0", "01", "040300", "USB", "Net", "Display", "Image", "MEDIA", "Camera",
    "Bluetooth", "Biometric", "SCSIAdapter", "stornvme", "hub", "Wi-Fi", "Wireless", "\xc3\xa9", "\xff\xfe", "\"", "\\", "\t" };

static void rstr(char *out, size_t n)
{
    size_t j = 0, want = rnd() % (n + 20);       /* sometimes longer than the buffer it is going into */
    while (j < want && j + 8 < n + 20 && j + 1 < 400) {
        const char *t = (rnd() % 5) ? TOK[rnd() % (sizeof TOK / sizeof TOK[0])] : "";
        if (!*t) { out[j++] = (char)(rnd() % 255 + 1); continue; }
        for (; *t && j + 1 < 400; t++) out[j++] = *t;
    }
    out[j] = 0;
}

int main(void)
{
    char a[400], b[400], c[400], d[400], e[400];
    for (int round = 0; round < 120; round++) {
        hw_inv *v = hwinv_new();
        int n = (int)(rnd() % 40);
        if (round % 20 == 7) n = HW_MAX_DEVS + 60;       /* more PCI devices than the table holds */
        for (int i = 0; i < n; i++) {
            rstr(a, 96); rstr(b, 400); rstr(c, 96); rstr(d, 48); rstr(e, 32);
            hw_id id; hwinv_parse_hwid(a, &id); hwinv_parse_cc(b);
            if (n > 100) snprintf(a, sizeof a, "PCI\\VEN_%04X&DEV_%04X", rnd() & 0xFFFF, rnd() & 0xFFFF);   /* kept */
            hwinv_add_dev(v, d, a, b, c, d, e, (int)(rnd() % 40), (int)(rnd() % 3) * 35);
        }
        n = (int)(rnd() % 30);
        hw_usbprn u[HW_MAX_PRINTERS]; int nu = 0;
        for (int i = 0; i < n; i++) {
            rstr(a, 96); rstr(b, 96); rstr(c, 64);
            hwinv_add_printer(v, a, b, c, (int)(rnd() & 1));
            if (nu < HW_MAX_PRINTERS && (rnd() & 1)) {
                memset(&u[nu], 0, sizeof u[nu]);
                u[nu].vid = (uint16_t)rnd(); u[nu].pid = (uint16_t)rnd();
                rstr(d, 96); snprintf(u[nu].ieee, sizeof u[nu].ieee, "%s", d); nu++;
            }
        }
        hwinv_pair_usb_printers(v, u, nu);
        hwcompat_judge(v);
        char *t = hwinv_text(v, "fuzz", "now"), *j = hwinv_json(v, "fuzz", "now");
        char line[128];
        hwinv_journal(v, line, sizeof line);
        if (!t || !j || strlen(line) > 127) { fprintf(stderr, "fuzz: bad output in round %d\n", round); return 1; }
        free(t); free(j);
        hwinv_free(v);
    }
    puts("fuzz: 120 random PCs, no crash");
    return 0;
}
