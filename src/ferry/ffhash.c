/* ═══════════════════════════════════════════════════════════════════
 *  ferry-ffhash — the name Firefox gives an install in profiles.ini.
 *
 *  Since Firefox 67 each INSTALL of Firefox has its own default
 *  profile, remembered in profiles.ini as [Install<HASH>] Default=...
 *  (and in installs.ini as [<HASH>]). A profile that is merely marked
 *  Default=1 is NOT opened by an install with no section of its own:
 *  Firefox makes a new empty one instead (tested against Firefox 158;
 *  this is why a copied Windows profile "lost all my bookmarks"). To
 *  have Firefox open the profile we brought across, Ferry writes that
 *  section, and the section is named by this hash.
 *
 *  <HASH> is CityHash64 (the 2011 version, v1.0.3) of the UTF-16
 *  directory holding the firefox binary, as 16 uppercase hex digits:
 *  /usr/lib/firefox -> 4F96D1932A9F858E (read from a real Firefox).
 *
 *    ferry-ffhash /usr/lib/firefox
 *
 *  exit 0 · 2 bad usage, a path that is not valid UTF-8, or one longer
 *  than 32 characters (the hash is unconfirmed beyond that)
 * ═══════════════════════════════════════════════════════════════════ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define K0 0xc3a5c85c97cb3127ULL
#define K1 0xb492b66fbe98f273ULL
#define K2 0x9ae16a3b2f90404fULL
#define K3 0xc949d7c7509e6557ULL

static uint64_t f64(const unsigned char *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
    return v;
}
static uint32_t f32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t rot(uint64_t v, int s) { return s == 0 ? v : (v >> s) | (v << (64 - s)); }
static uint64_t shiftmix(uint64_t v) { return v ^ (v >> 47); }
static uint64_t hash16(uint64_t u, uint64_t v)
{
    const uint64_t mul = 0x9ddfea08eb382d69ULL;
    uint64_t a = (u ^ v) * mul;
    a ^= a >> 47;
    uint64_t b = (v ^ a) * mul;
    b ^= b >> 47;
    return b * mul;
}

static uint64_t len0to16(const unsigned char *s, size_t n)
{
    if (n > 8) {
        uint64_t a = f64(s), b = f64(s + n - 8);
        return hash16(a, rot(b + n, (int)n)) ^ b;
    }
    if (n >= 4)
        return hash16(n + ((uint64_t)f32(s) << 3), f32(s + n - 4));
    if (n > 0) {
        uint32_t y = (uint32_t)s[0] + ((uint32_t)s[n >> 1] << 8);
        uint32_t z = (uint32_t)n + ((uint32_t)s[n - 1] << 2);
        return shiftmix(y * K2 ^ z * K3) * K2;
    }
    return K2;
}
static uint64_t len17to32(const unsigned char *s, size_t n)
{
    uint64_t a = f64(s) * K1, b = f64(s + 8);
    uint64_t c = f64(s + n - 8) * K2, d = f64(s + n - 16) * K0;
    return hash16(rot(a - b, 43) + rot(c, 30) + d, a + rot(b ^ K3, 20) - c + n);
}
struct pair { uint64_t first, second; };
static struct pair weak(uint64_t w, uint64_t x, uint64_t y, uint64_t z, uint64_t a, uint64_t b)
{
    a += w;
    b = rot(b + a + z, 21);
    uint64_t c = a;
    a += x;
    a += y;
    b += rot(a, 44);
    return (struct pair){ a + z, b + c };
}
static struct pair weak_s(const unsigned char *s, uint64_t a, uint64_t b)
{
    return weak(f64(s), f64(s + 8), f64(s + 16), f64(s + 24), a, b);
}
static uint64_t len33to64(const unsigned char *s, size_t n)
{
    uint64_t z = f64(s + 24);
    uint64_t a = f64(s) + (n + f64(s + n - 16)) * K0;
    uint64_t b = rot(a + z, 52), c = rot(a, 37);
    a += f64(s + 8);
    c += rot(a, 7);
    a += f64(s + 16);
    uint64_t vf = a + z, vs = b + rot(a, 31) + c;
    a = f64(s + 16) + f64(s + n - 32);
    z = f64(s + n - 8);
    b = rot(a + z, 52);
    c = rot(a, 37);
    a += f64(s + n - 24);
    c += rot(a, 7);
    a += f64(s + n - 16);
    uint64_t wf = a + z, ws = b + rot(a, 31) + c;
    uint64_t r = shiftmix((vf + ws) * K2 + (wf + vs) * K0);
    return shiftmix(r * K0 + vs) * K2;
}

uint64_t cityhash64(const unsigned char *s, size_t n)
{
    if (n <= 32) return n <= 16 ? len0to16(s, n) : len17to32(s, n);
    if (n <= 64) return len33to64(s, n);

    uint64_t x = f64(s), y = f64(s + n - 16) ^ K1, z = f64(s + n - 56) ^ K0;
    struct pair v = weak_s(s + n - 64, n, y), w = weak_s(s + n - 32, n * K1, K0);
    z += shiftmix(v.second) * K1;
    x = rot(z + x, 39) * K1;
    y = rot(y, 33) * K1;
    n = (n - 1) & ~(size_t)63;
    do {
        x = rot(x + y + v.first + f64(s + 16), 37) * K1;
        y = rot(y + v.second + f64(s + 48), 42) * K1;
        x ^= w.second;
        y ^= v.first;
        z = rot(z ^ w.first, 33);
        v = weak_s(s, v.second * K1, x + w.first);
        w = weak_s(s + 32, z + w.second, y);
        uint64_t t = z; z = x; x = t;
        s += 64;
        n -= 64;
    } while (n != 0);
    return hash16(hash16(v.first, w.first) + shiftmix(y) * K1 + z,
                  hash16(v.second, w.second) + x);
}

/* UTF-8 to UTF-16LE bytes; NULL for anything that is not UTF-8. */
static unsigned char *utf16le(const char *u, size_t *bytes)
{
    size_t n = strlen(u), o = 0;
    unsigned char *out = malloc(n * 4 + 2);
    if (!out) return NULL;
    for (size_t i = 0; i < n;) {
        unsigned c = (unsigned char)u[i];
        unsigned long cp;
        size_t k;
        if (c < 0x80) { cp = c; k = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; k = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; k = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; k = 3; }
        else { free(out); return NULL; }
        if (i + k >= n + (k == 0)) { free(out); return NULL; }
        for (size_t j = 1; j <= k; j++) {
            if (((unsigned char)u[i + j] & 0xC0) != 0x80) { free(out); return NULL; }
            cp = cp << 6 | ((unsigned char)u[i + j] & 0x3F);
        }
        i += k + 1;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            unsigned hi = 0xD800 + (unsigned)(cp >> 10), lo = 0xDC00 + (unsigned)(cp & 0x3FF);
            out[o++] = hi & 0xFF; out[o++] = hi >> 8;
            out[o++] = lo & 0xFF; out[o++] = lo >> 8;
        } else {
            out[o++] = cp & 0xFF; out[o++] = (unsigned char)(cp >> 8);
        }
    }
    *bytes = o;
    return out;
}

#ifndef FFHASH_NO_MAIN
int main(int argc, char **argv)
{
    size_t n;
    unsigned char *w;
    if (argc != 2) { fprintf(stderr, "usage: ferry-ffhash INSTALLDIR\n"); return 2; }
    w = utf16le(argv[1], &n);
    if (!w) { fprintf(stderr, "ferry-ffhash: not a UTF-8 path\n"); return 2; }
    /* Checked against real Firefoxes up to 32 characters (64 bytes of
     * UTF-16). Longer paths take another branch of the hash that no
     * real value has confirmed, and a wrong hash silently loses her
     * bookmarks: refuse rather than guess. */
    if (n > 64) {
        fprintf(stderr, "ferry-ffhash: install path longer than 32 characters\n");
        free(w);
        return 2;
    }
    {   /* a buffer, not printf straight from the hash: the image's hardening
         * gate wants every program to carry a stack protector, and the
         * compiler only adds one to a function that has an array */
        char hex[24];
        snprintf(hex, sizeof hex, "%016llX", (unsigned long long)cityhash64(w, n));
        puts(hex);
    }
    free(w);
    return 0;
}
#endif
