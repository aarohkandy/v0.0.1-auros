#ifndef AURBRIDGE_PROFILE_IMAGE_H
#define AURBRIDGE_PROFILE_IMAGE_H

#include <stddef.h>

/* Turn one of the five checked-in image profiles into its fixed basename.
 * This is deliberately an enum-by-string, not a path sanitizer: no recipe or
 * caller may supply a filename, a separator, or a new profile implicitly. */
int ab_profile_image_name(const char *profile, char *out, size_t cap);

#endif
