#include "profile_image.h"

#include <string.h>

int ab_profile_image_name(const char *profile, char *out, size_t cap)
{
    static const char *const allowed[] = {
        "desktop", "multilingual", "office", "revive", "school-kiosk"
    };
    static const char prefix[] = "auros-";
    static const char suffix[] = ".img";
    size_t i, pn, need;

    if (!out || cap == 0) return -1;
    out[0] = 0;
    if (!profile) return -1;

    for (i = 0; i < sizeof allowed / sizeof allowed[0]; i++)
        if (strcmp(profile, allowed[i]) == 0) break;
    if (i == sizeof allowed / sizeof allowed[0]) return -1;

    pn = strlen(profile);
    need = (sizeof prefix - 1) + pn + (sizeof suffix - 1) + 1;
    if (need > cap) return -1;
    memcpy(out, prefix, sizeof prefix - 1);
    memcpy(out + sizeof prefix - 1, profile, pn);
    memcpy(out + sizeof prefix - 1 + pn, suffix, sizeof suffix);
    return 0;
}
