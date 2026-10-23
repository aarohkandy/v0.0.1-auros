/* libFuzzer harness: the name a Windows file gets on her side.
 *
 * Any byte string Windows (via the NTFS driver) can hand us, at any
 * collision count. The name must fit ext4 (255 bytes), contain no '/',
 * never be empty, "." or "..", and be the original when nothing forced
 * a change. A shortened name must not split a UTF-8 character of a
 * valid name. Abort on any breach, so the fuzzer records it. */
#define main copy_main
#include "../copy.c"
#undef main

/* Structure only (lead byte, then its continuation bytes): exactly
 * what make_name must not cut through. */
static int valid_utf8(const unsigned char *s, size_t n)
{
    for (size_t i = 0; i < n;) {
        unsigned c = s[i];
        size_t k = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2
                 : (c & 0xF8) == 0xF0 ? 3 : 4;
        if (k == 4 || i + k >= n + (k == 0)) return 0;
        for (size_t j = 1; j <= k; j++) if ((s[i + j] & 0xC0) != 0x80) return 0;
        i += k + 1;
    }
    return 1;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
    char name[800], out[NAME_LIMIT + 1];
    size_t n = 0;
    if (size < 2) return 0;
    int count = data[0] % 120;
    for (size_t i = 1; i < size && n < 765; i++)
        if (data[i] && data[i] != '/') name[n++] = (char)data[i];
    name[n] = '\0';
    if (!n || !strcmp(name, ".") || !strcmp(name, "..")) return 0;

    int shortened = make_name(name, count, out);
    size_t len = strlen(out);
    if (len == 0 || len > NAME_LIMIT) abort();
    if (strchr(out, '/')) abort();
    if (!strcmp(out, ".") || !strcmp(out, "..")) abort();
    if (count == 0 && !shortened && strcmp(out, name)) abort();
    if (n <= NAME_LIMIT - 30 && shortened) abort();      /* shortened for no reason */
    if (valid_utf8((const unsigned char *)name, n) &&
        !valid_utf8((const unsigned char *)out, len)) abort();
    return 0;
}
