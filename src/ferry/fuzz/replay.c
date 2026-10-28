/* Replay driver: run a fuzz harness over files, once each, with no
 * fuzzing engine. Lets the ordinary test suite (gcc, no clang) build a
 * harness with AddressSanitizer + UndefinedBehaviorSanitizer and feed
 * it the inputs that once broke something and a few thousand corrupted
 * ones. A sanitizer report aborts: the suite sees a non-zero exit.
 *
 *   gcc -fsanitize=address,undefined -fno-sanitize-recover=undefined \
 *       -o replay-hive fuzz/hive.c fuzz/replay.c && ./replay-hive FILE...
 */
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);
__attribute__((weak)) int LLVMFuzzerInitialize(int *argc, char ***argv);

int main(int argc, char **argv)
{
    long n = 0;
    if (LLVMFuzzerInitialize) LLVMFuzzerInitialize(&argc, &argv);
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        unsigned char *buf;
        long sz;
        if (!f) continue;
        fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
        buf = malloc(sz > 0 ? (size_t)sz : 1);          /* exact size: ASan sees an over-read */
        if (buf && fread(buf, 1, (size_t)sz, f) == (size_t)sz) { LLVMFuzzerTestOneInput(buf, (size_t)sz); n++; }
        free(buf);
        fclose(f);
    }
    fprintf(stderr, "replayed %ld inputs\n", n);
    return 0;
}
