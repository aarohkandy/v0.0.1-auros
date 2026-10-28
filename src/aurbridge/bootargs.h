#ifndef AURBRIDGE_BOOTARGS_H
#define AURBRIDGE_BOOTARGS_H

#include <stddef.h>

/* The first desktop keeps the staging kernel. Keep these in agreement
 * with the normal and fallback AurOS boot entries (bootargstest). */
#define AB_KERNEL_HARDENING \
    "init_on_alloc=1 slab_nomerge randomize_kstack_offset=on " \
    "vsyscall=none debugfs=off panic=10 fsck.repair=yes lockdown=integrity"

int ab_stage_cmdline(char *out, size_t cap, const char *profile,
                     const char *extra, int direct);
const char *ab_stage_test_args(void);

#endif
