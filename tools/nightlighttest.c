/* nightlighttest.c — pixels, bounds and cost of the CPU night-light path. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/aurshell/nightlight.h"

/* nightlight_set() sizes its frame to include the persistent band. This pure
 * pixel test does not open the band, so keep that unrelated dependency local. */
int foot_height(const shell_ctx *c) { (void)c; return 0; }

static int checked, failed;
static void ok(const char *what, int good)
{
    checked++;
    if (!good) failed++;
    printf("  %-64s %s\n", what, good ? "ok" : "FAIL");
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1000000.0;
}

static void production_wiring(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { ok("the production frame loop can be read", 0); return; }
    char *s = malloc(256 * 1024);
    size_t n = s ? fread(s, 1, 256 * 1024 - 1, f) : 0;
    fclose(f);
    if (!s) { ok("the production frame loop can be read", 0); return; }
    s[n] = 0;

    const char *begin = strstr(s, "surface *frame = nightlight_frame(&c, fb);");
    const char *body = begin ? strstr(begin, "surface body = *frame;") : NULL;
    const char *paint = body ? strstr(body, "L->paint(&c, &body, &f, wall);") : NULL;
    const char *full = paint ? strstr(paint, "session_paint_full(&c, &body);") : NULL;
    const char *popups = full ? strstr(full, "session_paint_popups(&c, &body);") : NULL;
    const char *notify = popups ? strstr(popups, "notify_paint(&c, frame, &f)") : NULL;
    const char *net = notify ? strstr(notify, "net_paint(&c, frame, &f);") : NULL;
    const char *settings = net ? strstr(net, "settings_paint(&c, frame, &f);") : NULL;
    const char *bt = settings ? strstr(settings, "bt_paint(&c, frame, &f);") : NULL;
    const char *wish = bt ? strstr(bt, "wish_paint(&c, frame, &f);") : NULL;
    const char *welcome = wish ? strstr(wish, "welcome_paint(&c, frame, &f);") : NULL;
    const char *guard = welcome ? strstr(welcome, "guard_paint(&c, frame, &f);") : NULL;
    const char *foot = guard ? strstr(guard, "foot_paint(&c, frame, &f);") : NULL;
    const char *wishover = foot ? strstr(foot, "wishover_paint(&c, frame, &f);") : NULL;
    const char *osd = wishover ? strstr(wishover, "osd_paint(&c, frame, &f);") : NULL;
    const char *shot = osd ? strstr(osd, "png_write_rgb(path, frame->px, frame->w, frame->h)") : NULL;
    const char *cursor = shot ? strstr(shot, "session_paint_cursor(&c, frame);") : NULL;
    const char *present = cursor ? strstr(cursor, "nightlight_present(&c, fb);") : NULL;
    const char *flip = present ? strstr(present, "kms_flip(disp)") : NULL;
    ok("production paints an untinted frame, screenshots it, then warms every displayed layer",
       begin && body && paint && full && popups && notify && net && settings && bt &&
       wish && welcome && guard && foot && wishover && osd && shot && cursor && present && flip);
    ok("a shell action schedules the frame that presents its new state",
       strstr(s, "if (wish_step(&c)) dirty = 1;") != NULL);
    free(s);
}

int main(int argc, char **argv)
{
    uint32_t in_mem[12], out_mem[14];
    for (int i = 0; i < 12; i++) in_mem[i] = 0xA5A5A5A5u;
    for (int i = 0; i < 14; i++) out_mem[i] = 0xDEADBEEFu;
    surface src = { in_mem + 1, 3, 2, 5 };
    surface dst = { out_mem + 1, 3, 2, 6 };
    src.px[0] = 0x7F804020u;
    src.px[1] = 0xFFFFFFFFu;
    src.px[2] = 0xFF000000u;
    src.px[5] = 0xFF123456u;
    src.px[6] = 0x00102030u;
    src.px[7] = 0xFFA0B0C0u;

    ok("a warm copy succeeds", nightlight_blit(&dst, &src, 1) == 0);
    ok("red and alpha stay exact while green and blue become warmer",
       dst.px[0] == 0x7F803B17u && dst.px[1] == 0xFFFFEBB8u &&
       dst.px[2] == 0xFF000000u && dst.px[6] == 0xFF12303Eu &&
       dst.px[7] == 0x00101D23u && dst.px[8] == 0xFFA0A28Bu);
    ok("row padding and both outer canaries are untouched",
       out_mem[0] == 0xDEADBEEFu && out_mem[4] == 0xDEADBEEFu &&
       out_mem[5] == 0xDEADBEEFu && out_mem[6] == 0xDEADBEEFu &&
       out_mem[10] == 0xDEADBEEFu && out_mem[11] == 0xDEADBEEFu &&
       out_mem[12] == 0xDEADBEEFu && out_mem[13] == 0xDEADBEEFu);

    memset(out_mem, 0, sizeof out_mem);
    ok("off copies the canonical pixels back exactly",
       nightlight_blit(&dst, &src, 0) == 0 &&
       !memcmp(dst.px, src.px, 3 * sizeof(uint32_t)) &&
       !memcmp(dst.px + dst.stride, src.px + src.stride, 3 * sizeof(uint32_t)));
    ok("an in-place pass is refused so tint cannot accumulate",
       nightlight_blit(&src, &src, 1) < 0);
    surface short_row = src;
    short_row.stride = 2;
    ok("an impossible stride is refused without writing",
       nightlight_blit(&dst, &short_row, 1) < 0);

    shell_ctx c;
    memset(&c, 0, sizeof c);
    c.screen_w = 3;
    c.screen_h = 2;
    ok("turning on allocates a separate canonical frame",
       nightlight_set(&c, 1) && c.night_frame && c.night_light &&
       nightlight_frame(&c, &dst) == c.night_frame && c.night_frame->px != dst.px);
    if (c.night_frame) {
        surface_fill(c.night_frame, 0xFF204060u);
        nightlight_present(&c, &dst);
        uint32_t once = dst.px[0];
        surface_fill(c.night_frame, 0xFF204060u);
        nightlight_present(&c, &dst);
        ok("two presents derive from untinted pixels instead of accumulating",
           once == 0xFF203B45u && dst.px[0] == once && c.night_frame->px[0] == 0xFF204060u);
        ok("turning off copies the canonical frame without colour changes",
           nightlight_set(&c, 0) && !c.night_light &&
           (nightlight_present(&c, &dst), dst.px[0] == 0xFF204060u));
    }
    nightlight_fini(&c);
    ok("the staging frame is released", !c.night_frame && !c.night_light);

    const int w = 1920, h = 1080, rounds = 120;
    surface *a = surface_new(w, h), *b = surface_new(w, h);
    ok("the timing frame can be allocated", a && b);
    if (a && b) {
        surface_fill(a, 0xFF789ABCu);
        double t0 = now_ms();
        for (int i = 0; i < rounds; i++) nightlight_blit(b, a, 1);
        double elapsed = now_ms() - t0;
        double per = elapsed / rounds;
        printf("    1920x1080 warm copy: %.3f ms/frame (%.1f million pixels/s, n=%d)\n",
               per, (double)w * h / (per * 1000.0), rounds);
        ok("the benchmark ran for measurable time", elapsed > 0.0 && b->px[0] == 0xFF788E88u);
    }
    surface_free(a);
    surface_free(b);
    production_wiring(argc > 1 ? argv[1] : "src/aurshell/main.c");

    printf("\n%d checks, %d failed\n", checked, failed);
    return failed ? 1 : 0;
}
