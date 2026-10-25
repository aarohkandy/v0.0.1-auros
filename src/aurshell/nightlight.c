/* nightlight.c — see nightlight.h. */
#include <stddef.h>
#include <stdint.h>

#include "nightlight.h"
#include "foot.h"

static int usable(const surface *s)
{
    return s && s->px && s->w > 0 && s->h > 0 && s->stride >= s->w &&
           (size_t)s->h <= SIZE_MAX / (size_t)s->stride;
}

int nightlight_blit(surface *dst, const surface *src, int on)
{
    if (!usable(dst) || !usable(src) || dst->w != src->w || dst->h != src->h ||
        dst->px == src->px)
        return -1;

    for (int y = 0; y < src->h; y++) {
        const uint32_t *sp = src->px + (size_t)y * src->stride;
        uint32_t *dp = dst->px + (size_t)y * dst->stride;
        if (!on) {
            for (int x = 0; x < src->w; x++) dp[x] = sp[x];
            continue;
        }
        for (int x = 0; x < src->w; x++) {
            uint32_t p = sp[x];
            uint32_t g = ((p >> 8) & 255u) * 235u;
            uint32_t b = (p & 255u) * 184u;
            g = (g + 127u) / 255u;
            b = (b + 127u) / 255u;
            /* Preserve alpha and red. The final framebuffer is opaque, but
             * preserving alpha makes the helper safe for test and preview
             * surfaces too. */
            dp[x] = (p & 0xFFFF0000u) | (g << 8) | b;
        }
    }
    return 0;
}

int nightlight_set(shell_ctx *c, int on)
{
    if (!c || (on != 0 && on != 1)) return 0;
    if (!c->night_frame && on) {
        int h = c->screen_h + foot_height(c);
        if (c->screen_w <= 0 || h <= 0) return 0;
        c->night_frame = surface_new(c->screen_w, h);
        if (!c->night_frame) return 0;
    }
    c->night_light = on;
    return 1;
}

surface *nightlight_frame(shell_ctx *c, surface *display)
{
    if (!c || !display || !c->night_frame) return display;
    if (c->night_frame->w != display->w || c->night_frame->h != display->h)
        return display;
    return c->night_frame;
}

void nightlight_present(shell_ctx *c, surface *display)
{
    if (!c || !display || !c->night_frame) return;
    (void)nightlight_blit(display, c->night_frame, c->night_light);
}

void nightlight_fini(shell_ctx *c)
{
    if (!c) return;
    surface_free(c->night_frame);
    c->night_frame = NULL;
    c->night_light = 0;
}
