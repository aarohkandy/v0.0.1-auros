#ifndef AURBRIDGE_PROFILE_IMAGE_H
#define AURBRIDGE_PROFILE_IMAGE_H

#include <stddef.h>

/* 1 if this is a name a profile may have: [a-z0-9-], 1-32 characters, not
 * starting or ending with a dash. The only gate between a profile name and
 * a file name, a journal field or a manifest field. */
int ab_profile_name_ok(const char *profile);

/* Turn a profile name into its fixed basename "auros-<profile>.img", or fail
 * closed (-1, empty output) for anything ab_profile_name_ok refuses. No
 * recipe or caller may supply a filename or a separator. */
int ab_profile_image_name(const char *profile, char *out, size_t cap);

#endif
