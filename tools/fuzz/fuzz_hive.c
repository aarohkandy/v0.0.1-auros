/* fuzz_hive.c -- Ferry's registry reader against a hive someone else wrote.
 *
 * src/ferry/hivepeek.c (stream R's file; S3 only fuzzes it) reads
 * NTUSER.DAT, SOFTWARE and SYSTEM off her Windows disk, AS ROOT, on the
 * first boot of AurOS. Those files are whatever Windows, a crash, a
 * disk going bad or a program with her rights left there. The fuzzer's
 * bytes become the hive, and the real main() is run the four ways
 * ferry-hive runs it (values, one value, one value with its type,
 * subkeys), against a key path the input also picks.
 *
 * Input: [mode byte] KEYPATH '\n' [VALUENAME '\n'] HIVE
 *   mode & 3: 0 list values, 1 -k subkeys, 2 -t VALUENAME, 3 VALUENAME
 */
#define _GNU_SOURCE
#include <stdio.h>
/* hivepeek complains on stderr about every broken hive, which here is
 * nearly every input. Its stderr, not the process's: the sanitizers
 * still report on fd 2. */
static FILE *hive_err;
#undef stderr
#define stderr hive_err
#define main hivepeek_main
#include "../../src/ferry/hivepeek.c"
#undef main
#undef stderr
#define stderr stderr

#include <unistd.h>
#include <sys/mman.h>

static int  memfd = -1;
static char memfd_path[64];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2 || size > (4u << 20)) return 0;
    if (memfd < 0) {
        /* hivepeek prints what it finds; nobody is reading. */
        if (!freopen("/dev/null", "w", stdout)) abort();
        hive_err = fopen("/dev/null", "w");
        if (!hive_err) abort();
        memfd = memfd_create("fuzz-hive", MFD_CLOEXEC);
        if (memfd < 0) abort();
        snprintf(memfd_path, sizeof memfd_path, "/proc/self/fd/%d", memfd);
    }
    int mode = data[0] & 3;
    const uint8_t *p = data + 1, *end = data + size;

    char key[512], name[256];
    const uint8_t *nl = memchr(p, '\n', (size_t)(end - p));
    if (!nl || (size_t)(nl - p) >= sizeof key) return 0;
    memcpy(key, p, (size_t)(nl - p)); key[nl - p] = 0;
    p = nl + 1;
    if (mode >= 2) {
        nl = memchr(p, '\n', (size_t)(end - p));
        if (!nl || (size_t)(nl - p) >= sizeof name) return 0;
        memcpy(name, p, (size_t)(nl - p)); name[nl - p] = 0;
        p = nl + 1;
    }
    if (ftruncate(memfd, 0) < 0) abort();
    if (end > p && pwrite(memfd, p, (size_t)(end - p), 0) != end - p) abort();

    char *argv[6]; int argc = 0;
    argv[argc++] = "ferry-hivepeek";
    if (mode == 1) argv[argc++] = "-k";
    if (mode == 2) argv[argc++] = "-t";
    argv[argc++] = memfd_path;
    argv[argc++] = key;
    if (mode >= 2) argv[argc++] = name;
    argv[argc] = NULL;

    img = NULL; imglen = 0;
    (void)hivepeek_main(argc, argv);
    free(img);               /* the real program exits here instead */
    img = NULL;
    return 0;
}
