#include "profile_image.h"

#include <string.h>

/* The profile is fixed when an installer is BUILT (tools/customize/
 * installer_profile.py reads profiles/<name>.profile), and a new profile is a
 * new file, not an edit here. So the name is held to a strict pattern
 * rather than to a list: lower-case letters, digits and single dashes, 1-32
 * characters, starting and ending with a letter or digit. That keeps what the
 * list was for: no separator, dot, colon, space or case can reach the image
 * file name "auros-<profile>.img", the journal's JSON, or the manifest, and
 * the fixed prefix and suffix mean no reserved Windows device name can be
 * formed. Anything else fails closed. */
int ab_profile_name_ok(const char *profile)
{
    size_t i, n;
    if (!profile) return 0;
    n = strlen(profile);
    if (n == 0 || n > 32) return 0;
    for (i = 0; i < n; i++) {
        char ch = profile[i];
        int alnum = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
        if (!alnum && ch != '-') return 0;
        if (ch == '-' && (i == 0 || i == n - 1)) return 0;
    }
    return 1;
}

int ab_profile_image_name(const char *profile, char *out, size_t cap)
{
    static const char prefix[] = "auros-";
    static const char suffix[] = ".img";
    size_t pn, need;

    if (!out || cap == 0) return -1;
    out[0] = 0;
    if (!ab_profile_name_ok(profile)) return -1;

    pn = strlen(profile);
    need = (sizeof prefix - 1) + pn + (sizeof suffix - 1) + 1;
    if (need > cap) return -1;
    memcpy(out, prefix, sizeof prefix - 1);
    memcpy(out + sizeof prefix - 1, profile, pn);
    memcpy(out + sizeof prefix - 1 + pn, suffix, sizeof suffix);
    return 0;
}
