/* nightlight.h — warm the completed screen without touching client buffers. */
#ifndef AUROS_NIGHTLIGHT_H
#define AUROS_NIGHTLIGHT_H

#include "shell.h"

/* Start using a separate, untinted frame and select normal or warm output.
 * Returns 0 only after the staging frame exists, so a wish never reports a
 * change that the display path cannot carry out. */
int nightlight_set(shell_ctx *c, int on);

/* The surface the shell must paint this frame. Before night light has ever
 * been enabled this is `display`; afterwards it is the untinted staging
 * surface, including while night light is off so no old tint can accumulate.
 * A display-size change replaces the staging surface. Allocation failure
 * turns night light off and returns NULL instead of claiming an untinted
 * active state. */
surface *nightlight_frame(shell_ctx *c, surface *display);

/* Copy a completed untinted frame to the scanout surface, warming it when on.
 * Padding pixels are not touched and source/destination must not overlap.
 * Exposed for the bounds/stride test. Presentation returns -1 on failure. */
int nightlight_blit(surface *dst, const surface *src, int on);
int nightlight_present(shell_ctx *c, surface *display);
void nightlight_fini(shell_ctx *c);

#endif
