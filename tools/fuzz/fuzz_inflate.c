/* fuzz_inflate.c -- AurBridge's gzip decoder against hostile pieces.
 *
 * src/aurbridge/inflate.c (stream A2's file; S3 only fuzzes it) decodes
 * the AurOS image as it arrives from the network, in pieces. The SHA-256
 * afterwards catches a wrong image, but not a decoder that writes past
 * its window, reads past its tables or loops forever on the way there:
 * that happens on her Windows PC, inside the installer, before any hash
 * is compared. So every byte of the stream is the fuzzer's, and so is
 * how it is cut into pieces.
 *
 * Input: [piece size byte][gzip member]. 0 hands the decoder everything
 * at once; 1..255 hands it that many bytes per read, so a Huffman code,
 * a stored block's LEN or the trailer can straddle a piece boundary.
 * Output is counted and dropped; past 16 MiB the writer stops the decode
 * (a 1 MiB input could otherwise inflate to a gigabyte), which also
 * exercises the "writer refused" path.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/aurbridge/inflate.h"

typedef struct { const uint8_t *p; size_t n, at, step; } src;

static int rd(void *ud, uint8_t *buf, size_t n, size_t *got)
{
    src *s = ud;
    size_t k = s->n - s->at;
    if (s->step && k > s->step) k = s->step;
    if (k > n) k = n;
    memcpy(buf, s->p + s->at, k);
    s->at += k;
    *got = k;
    return 0;
}

static int wr(void *ud, const uint8_t *b, size_t n)
{
    uint64_t *total = ud;
    (void)b;
    *total += n;
    return *total > (16u << 20);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > (1u << 20)) return 0;
    src s = { data + 1, size - 1, 0, data[0] };
    uint64_t seen = 0, out = 0;
    char why[256];
    if (gz_inflate(rd, &s, wr, &seen, &out, why, sizeof why) == 0 && out != seen)
        abort();                       /* claimed a length it did not hand on */
    return 0;
}
