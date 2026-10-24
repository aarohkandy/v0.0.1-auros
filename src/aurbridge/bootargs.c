#include "bootargs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *ab_stage_test_args(void)
{
    /* Only the host-side simulated installer may inject fault/debug
     * arguments. An elevated Windows executable inherits user input. */
#ifdef _WIN32
    return NULL;
#else
    return getenv("AURBRIDGE_STAGING_KARGS");
#endif
}

int ab_stage_cmdline(char *out, size_t cap, const char *profile,
                     const char *extra, int direct)
{
    if (!out || !cap) return -1;
    out[0] = '\0';
#ifdef _WIN32
    if (extra && *extra) return -1;
#endif
    if (!profile || !*profile) return -1;
    for (const unsigned char *p = (const unsigned char *)profile; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-'))
            return -1;
    /* The optional test arguments also appear inside grub.cfg. Never
     * let an environment variable inject another GRUB command. */
    if (extra) for (const unsigned char *p = (const unsigned char *)extra; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || strchr(" _-./=,:", *p)))
            return -1;
    int n = snprintf(out, cap,
        "%saurstage.install aurstage.profile=%s console=tty0 gpt quiet loglevel=3 "
        "vt.global_cursor_default=0%s%s " AB_KERNEL_HARDENING,
        direct ? "initrd=\\EFI\\AurOS\\staging.img " : "", profile,
        extra && *extra ? " " : "", extra && *extra ? extra : "");
    if (n < 0 || (size_t)n >= cap) { out[0] = '\0'; return -1; }
    return 0;
}
