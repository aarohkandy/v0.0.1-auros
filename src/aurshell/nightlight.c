/* nightlight.c — see nightlight.h. */
#include <stddef.h>
#include <stdint.h>

#include "nightlight.h"

static int usable(const surface *s)
{
    if (!s || !s->px || s->w <= 0 || s->h <= 0 || s->stride < s->w) return 0;
    if ((size_t)s->stride > SIZE_MAX / sizeof *s->px) return 0;
    size_t row_bytes = (size_t)s->stride * sizeof *s->px;
    return (size_t)s->h <= SIZE_MAX / row_bytes;
}

static int overlaps(const surface *a, const surface *b)
{
    /* Conservatively cover every byte from the first visible pixel through
     * the last. Row padding may make that interval wider than the bytes we
     * touch, which is preferable to accepting an ambiguous alias. */
    size_t an = ((size_t)(a->h - 1) * (size_t)a->stride + (size_t)a->w) *
                sizeof *a->px;
    size_t bn = ((size_t)(b->h - 1) * (size_t)b->stride + (size_t)b->w) *
                sizeof *b->px;
    uintptr_t ap = (uintptr_t)a->px, bp = (uintptr_t)b->px;
    if (an > UINTPTR_MAX - ap || bn > UINTPTR_MAX - bp) return 1;
    return ap < bp + bn && bp < ap + an;
}

int nightlight_blit(surface *dst, const surface *src, int on)
{
    if (!usable(dst) || !usable(src) || dst->w != src->w || dst->h != src->h ||
        overlaps(dst, src))
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
        if (c->output_w <= 0 || c->output_h <= 0) return 0;
        c->night_frame = surface_new(c->output_w, c->output_h);
        if (!c->night_frame) return 0;
    }
    c->night_light = on;
    return 1;
}

surface *nightlight_frame(shell_ctx *c, surface *display)
{
    if (!c || !display) return NULL;
    if (!c->night_frame) return display;
    if (c->night_frame->w != display->w || c->night_frame->h != display->h) {
        surface *replacement = surface_new(display->w, display->h);
        if (!replacement) {
            nightlight_fini(c);
            return NULL;
        }
        surface_free(c->night_frame);
        c->night_frame = replacement;
    }
    return c->night_frame;
}

int nightlight_present(shell_ctx *c, surface *display)
{
    if (!c || !display) return -1;
    if (!c->night_frame) return 0;
    return nightlight_blit(display, c->night_frame, c->night_light);
}

void nightlight_fini(shell_ctx *c)
{
    if (!c) return;
    surface_free(c->night_frame);
    c->night_frame = NULL;
    c->night_light = 0;
}
