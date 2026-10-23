/* main.c — aurwish, the Wishes engine on her computer (docs/AI.md §3).
 *
 * A user service: her uid, never root. It keeps her wishes (wishes.json),
 * listens for the world (time, typed words and opened programs from the
 * compositor via the shell, battery, USB, sensors) and does what a wish
 * says -- inside the caps in engine.c. It is the only program that talks
 * to the cloud. It talks to the shell over a private unix socket
 * ($XDG_RUNTIME_DIR/aurwish.sock, her uid only, docs/AI.md §3.7).
 *
 * OFF UNLESS THE MACHINE SAYS ON (owner decision D14). The program is not
 * in an image unless the profile asks for it, and when present it exits at
 * once unless /etc/auros/policy.conf says `wishes = on`. A crash or a bad
 * file never blocks login: this is a separate process and the shell does
 * not wait for it.
 *
 * It logs counts and reasons, never a wish, a word she typed, or a token. */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include "answer.h"
#include "cloud.h"
#include "engine.h"
#include "sensors.h"
#include "../aurshell/power.h"
#include "../aurshell/run.h"
#include "../common/theme.h"

#ifdef AURWISH_TEST
#define OVERRIDE(var, def) (getenv(var) && *getenv(var) ? getenv(var) : (def))
#else
#define OVERRIDE(var, def) (def)
#endif

#define MAX_CONNS 4
#define LINE_IN   4096           /* shell -> engine (§3.7) */
#define REPLY_MAX (64 * 1024)    /* engine -> shell */

static volatile sig_atomic_t want_quit;
static void on_term(int s) { (void)s; want_quit = 1; }

/* ── what the machine says ───────────────────────────────────────── */

/* `wishes = on` in the machine's policy; anything else, or no file, is off. */
static int policy_on(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    int on = 0;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        while (isspace((unsigned char)*k)) k++;
        for (char *e = k + strlen(k); e > k && isspace((unsigned char)e[-1]); ) *--e = 0;
        while (isspace((unsigned char)*v) || *v == '"') v++;
        for (char *e = v + strlen(v); e > v && (isspace((unsigned char)e[-1]) || e[-1] == '"'); ) *--e = 0;
        if (!strcmp(k, "wishes")) on = !strcmp(v, "on");
    }
    fclose(f);
    return on;
}

/* `url=https://…` from the signed package config (docs/CLOUD.md §6). */
static void read_conf(const char *path, char *url, size_t n)
{
    url[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[300];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "url=", 4) && strlen(line + 4) < n) memcpy(url, line + 4, strlen(line + 4) + 1);
    }
    fclose(f);
}

/* ── the shell's connections ─────────────────────────────────────── */

typedef struct { int fd; char buf[LINE_IN + 8]; size_t len; int hello; unsigned gen; } conn;
static conn conns[MAX_CONNS];
static unsigned conn_gen;

static void conn_close(conn *c)
{
    if (c->fd >= 0) close(c->fd);
    memset(c, 0, sizeof *c);
    c->fd = -1;
}

static int send_line(conn *c, const char *line)
{
    size_t l = strlen(line);
    if (c->fd < 0 || l + 1 > REPLY_MAX) return -1;
    char *tmp = malloc(l + 1);
    if (!tmp) return -1;
    memcpy(tmp, line, l);
    tmp[l] = '\n';
    size_t done = 0;
    int bad = 0;
    /* A shell that does not read is a shell that is gone: never wait for it. */
    for (int tries = 0; done < l + 1 && tries < 50; ) {
        ssize_t k = send(c->fd, tmp + done, l + 1 - done, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (k > 0) { done += (size_t)k; continue; }
        if (k < 0 && errno == EINTR) continue;
        if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { tries++; usleep(2000); continue; }
        bad = 1;
        break;
    }
    free(tmp);
    if (bad || done < l + 1) { conn_close(c); return -1; }
    return 0;
}

static int io_emit(void *ctx, const char *line)
{
    (void)ctx;
    int n = 0;
    for (int i = 0; i < MAX_CONNS; i++)
        if (conns[i].fd >= 0 && conns[i].hello && send_line(&conns[i], line) == 0) n++;
    return n;
}

/* ── her computer's controls ────────────────────────────────────── */

static const char *SOUNDS_DIR = "/usr/share/auros/sounds";

static int io_bright_get(void *c) { (void)c; return power_brightness(); }
static int io_bright_set(void *c, int p) { (void)c; return power_brightness_set(p); }
static int io_vol_get(void *c) { (void)c; power_refresh(); return power_volume(); }
static int io_vol_set(void *c, int p) { (void)c; power_volume_set(p); return 0; }

static int io_sound(void *c, int s)
{
    (void)c;
    char path[300];
    snprintf(path, sizeof path, "%s/%s.wav", SOUNDS_DIR, wish_sound_name(s));
    if (access(path, R_OK) != 0) return -1;
    /* Through her own sound server, so it is never louder than the level
     * she has set. */
    const char *argv[] = { "mpv", "--no-video", "--really-quiet", "--no-terminal", "--volume=70", path, NULL };
    return run_detached(argv);
}

static int io_say(void *c, const char *text)
{
    (void)c;
    /* W2's on-device voice. An argument vector, never a shell. */
    const char *argv[] = { "aurvoice", "say", text, NULL };
    return run_detached(argv);
}

/* §7.6: the camera or the microphone is on. Looked for only when
 * something is about to be shown, and not more than every 2 seconds. */
static unsigned io_holds(void *c)
{
    (void)c;
    static unsigned cached;
    static time_t at;
    time_t now = time(NULL);
    if (at && now - at < 2) return cached;
    at = now;
    unsigned h = 0;
#ifdef AURWISH_TEST
    if (getenv("AURWISH_FAKE_HOLD")) h |= (unsigned)atoi(getenv("AURWISH_FAKE_HOLD"));
#endif
    /* camera: one of our own programs has /dev/video* open */
    DIR *d = opendir("/proc");
    int procs = 0;
    struct dirent *e;
    while (d && !(h & H_CAMERA) && (e = readdir(d)) && procs < 800) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        procs++;
        char p[300];
        snprintf(p, sizeof p, "/proc/%.250s/fd", e->d_name);
        DIR *fd = opendir(p);
        struct dirent *f;
        int n = 0;
        while (fd && (f = readdir(fd)) && n++ < 128) {
            if (f->d_name[0] == '.') continue;
            char l[600], t[64];
            snprintf(l, sizeof l, "/proc/%.250s/fd/%.250s", e->d_name, f->d_name);
            ssize_t k = readlink(l, t, sizeof t - 1);
            if (k > 10) { t[k] = 0; if (!strncmp(t, "/dev/video", 10)) { h |= H_CAMERA; break; } }
        }
        if (fd) closedir(fd);
    }
    if (d) closedir(d);
    /* microphone: a capture stream is running */
    DIR *a = opendir("/proc/asound");
    while (a && (e = readdir(a))) {
        if (strncmp(e->d_name, "card", 4)) continue;
        for (int pcm = 0; pcm < 8; pcm++) {
            for (int sub = 0; sub < 4; sub++) {
                char p[400], line[64];
                snprintf(p, sizeof p, "/proc/asound/%.250s/pcm%dc/sub%d/status", e->d_name, pcm, sub);
                FILE *s = fopen(p, "r");
                if (!s) continue;
                if (fgets(line, sizeof line, s) && strstr(line, "RUNNING")) h |= H_MIC;
                fclose(s);
            }
        }
    }
    if (a) closedir(a);
    cached = h;
    return h;
}

static cloud *CL;
static int io_forget(void *c) { (void)c; if (CL) cloud_forget(CL); return 0; }

/* ── one request to the cloud at a time ─────────────────────────── */

static struct { int active; long long id; unsigned gen; int ci; char said[1024]; int voice; } cur;
static struct { unsigned long hash; long long at; } last_text;

static unsigned long hash_str(const char *s)
{
    unsigned long h = 5381;
    for (; *s; s++) h = h * 33 + (unsigned char)*s;
    return h;
}

static void reply_to(conn *c, const char *line) { if (line[0]) send_line(c, line); }

static void cloud_op(engine *E, store *ST, conn *c, int ci, const char *line, size_t len,
                     long long now, char *reply)
{
    static char mem[16384];
    static const jlimits L = { LINE_IN, 4, 40, 1024 };
    jarena a = { mem, sizeof mem, 0, NULL };
    const jv *m = json_parse(&a, &L, line, len);
    const jv *opv = jget(m, "op"), *idv = jget(m, "id");
    const char *op = opv && opv->type == J_STR ? opv->s : "";
    long long id = idv && idv->type == J_INT ? idv->i : 0;

    if (!strcmp(op, "cancel")) {
        if (cur.active && cur.id == id) { cloud_cancel(CL); cur.active = 0; }
        return;
    }
    int ask = !strcmp(op, "ask");
    const jv *tv = jget(m, ask ? "text" : "text");
    const char *text = tv && tv->type == J_STR ? tv->s : "";
    if (!cloud_available(CL)) { answer_error(id, "offline", 0, reply, REPLY_MAX); reply_to(c, reply); return; }
    if (!ST->consent) { answer_error(id, "consent", 0, reply, REPLY_MAX); reply_to(c, reply); return; }
    if (cur.active) { answer_error(id, "busy", 0, reply, REPLY_MAX); reply_to(c, reply); return; }
    char clean[1400];
    int cps = cloud_clean_text(text, clean, sizeof clean);
    int raw_cps = 0;
    for (const char *p = text; *p; p++) raw_cps += (*p & 0xC0) != 0x80;
    /* §7.9: nothing under 3 characters, nothing over 300, and the same
     * words again within 30 seconds are not sent a second time. */
    unsigned long hs = hash_str(text);
    if (cps < 0 || raw_cps < 3 || raw_cps > 300 ||
        (last_text.at && now - last_text.at < 30 && last_text.hash == hs)) {
        answer_error(id, "refused", 0, reply, REPLY_MAX);
        reply_to(c, reply);
        return;
    }
    const jv *lg = jget(m, "lang"), *lk = jget(m, "look"), *vc = jget(m, "voice");
    const char *err = NULL;
    if (cloud_start(CL, ask ? CLOUD_ASK : CLOUD_WISH, id, clean, lg && lg->type == J_STR ? lg->s : "en",
                    lk && lk->type == J_STR ? lk->s : NULL, now, &err) < 0) {
        answer_error(id, err ? err : "broken", 0, reply, REPLY_MAX);
        reply_to(c, reply);
        return;
    }
    last_text.hash = hs;
    last_text.at = now;
    cur.active = 1;
    cur.id = id;
    cur.ci = ci;
    cur.gen = c->gen;
    cur.voice = vc && vc->type == J_BOOL && vc->i;
    snprintf(cur.said, sizeof cur.said, "%s", text);
    (void)E;
}

/* ── the main loop ──────────────────────────────────────────────── */

static int listen_on(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof sa.sun_path) { close(fd); return -1; }
    strcpy(sa.sun_path, path);
    unlink(path);
    mode_t old = umask(077);                    /* 0600: her uid only */
    int r = bind(fd, (struct sockaddr *)&sa, sizeof sa);
    umask(old);
    if (r < 0 || listen(fd, 4) < 0) { close(fd); return -1; }
    chmod(path, 0600);
    return fd;
}

int main(int argc, char **argv)
{
    int force = 0;
    for (int i = 1; i < argc; i++) {
#ifdef AURWISH_TEST
        if (!strcmp(argv[i], "--force")) force = 1;
#endif
        if (!strcmp(argv[i], "--help")) {
            fputs("aurwish: the Wishes engine (docs/AI.md). Started by the user service.\n", stderr);
            return 0;
        }
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    umask(077);

    if (!force && !policy_on(OVERRIDE("AURWISH_POLICY", "/etc/auros/policy.conf"))) {
        fprintf(stderr, "aurwish: Wishes is off on this computer (policy)\n");
        return 0;                               /* 0: systemd does not restart it */
    }

    /* Everything below can fail without stopping her: no wishes file,
     * no cloud address, no sensors, no sockets to listen on beyond ours. */
    char spath[700], sock[300];
    const char *rd = getenv("XDG_RUNTIME_DIR");
    if (!rd || !*rd) { fprintf(stderr, "aurwish: no runtime directory\n"); return 1; }
    snprintf(sock, sizeof sock, "%s", OVERRIDE("AURWISH_SOCK", ""));
    if (!sock[0]) snprintf(sock, sizeof sock, "%s/aurwish.sock", rd);
    const char *sp = OVERRIDE("AURWISH_STORE", "");
    if (sp[0]) snprintf(spath, sizeof spath, "%s", sp);
    else if (store_path(spath, sizeof spath) < 0) { fprintf(stderr, "aurwish: no home\n"); return 1; }
    SOUNDS_DIR = OVERRIDE("AURWISH_SOUNDS", SOUNDS_DIR);
    const char *help_dir = OVERRIDE("AURWISH_HELP", "/usr/share/auros/help");

    static wish_env env;
    memset(&env, 0, sizeof env);
    env.themes_dir = getenv("AUROS_THEME_DIR");
    env.fg = 0xF3F7FD;
    {   /* her text colour, for the wallpaper contrast check (§7.4) */
        char tp[600];
        const char *home = getenv("HOME");
        theme_t t;
        memset(&t, 0, sizeof t);
        snprintf(tp, sizeof tp, "%s/.config/auros/shell.conf", home ? home : "");
        if (theme_load(&t, tp) < 0) theme_load(&t, "/etc/auros/shell.conf");
        env.fg = theme_color(&t, "col_fg", 0xF3F7FD) & 0xFFFFFF;
    }

    static store ST;
    int bad = store_load(&ST, spath, &env);
    if (bad) fprintf(stderr, "aurwish: wishes.json could not be read; kept aside, starting empty\n");

    cloud_cfg cc;
    memset(&cc, 0, sizeof cc);
    read_conf(OVERRIDE("AURWISH_CONF", "/usr/share/auros/wishes.conf"), cc.url, sizeof cc.url);
#ifdef AURWISH_TEST
    cc.allow_http = 1;
#endif
    {
        char tp[640];
        snprintf(tp, sizeof tp, "%.600s", spath);
        char *sl = strrchr(tp, '/');
        if (sl) *sl = 0;
        snprintf(cc.token_path, sizeof cc.token_path, "%.600s/wishes-token.json", tp);
    }
    cc.consent_version = ST.consent ? ST.consent : 1;
    CL = cloud_new(&cc);

    sensors *SN = sensors_open(OVERRIDE("AURWISH_DBUS", NULL));
    unsigned have = sensors_have(SN, time(NULL), 1);
    cc.sensors = have;
    env.sensors = have;
    cloud_set_sensors(CL, have);
    store_recheck(&ST, &env);

    engine_io io = { io_emit, io_bright_get, io_bright_set, io_vol_get, io_vol_set,
                     io_sound, io_say, NULL, io_forget, io_holds };
    engine *E = engine_new(&ST, spath, &env, &io);
    if (!E || !CL) return 1;

    for (int i = 0; i < MAX_CONNS; i++) conns[i].fd = -1;
    int lfd = listen_on(sock);
    if (lfd < 0) { fprintf(stderr, "aurwish: cannot listen on %s\n", sock); return 1; }
    int ufd = uevent_open();                     /* USB; -1 in a container, fine */

    static char reply[REPLY_MAX];
    static char trig[4096];
    int started = 0;
    long long last_tick = 0, last_bat = 0, last_usb = 0;
    unsigned claimed = 0;

    while (!want_quit) {
        long long now = (long long)time(NULL);
        int fast = engine_wants_fast(E, now);
        int slow = 10000;                       /* nothing waiting: look every 10 s */
#ifdef AURWISH_TEST
        if (getenv("AURWISH_SLOW_TICK_MS")) slow = atoi(getenv("AURWISH_SLOW_TICK_MS"));
#endif
        int timeout = cloud_cpu(CL) ? 10 : fast ? 1000 : slow;
        struct pollfd pf[MAX_CONNS + 3];
        int np = 0;
        pf[np++] = (struct pollfd){ lfd, POLLIN, 0 };
        for (int i = 0; i < MAX_CONNS; i++) pf[np++] = (struct pollfd){ conns[i].fd, POLLIN, 0 };
        int cfd = cloud_fd(CL);
        pf[np++] = (struct pollfd){ cfd, POLLIN, 0 };
        pf[np++] = (struct pollfd){ ufd, POLLIN, 0 };
        int pr = poll(pf, (nfds_t)np, timeout);
        now = (long long)time(NULL);
        (void)pr;

        if (pf[0].revents & POLLIN) {
            int nfd = accept4(lfd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
            if (nfd >= 0) {
                struct ucred uc;
                socklen_t ul = sizeof uc;
                int slot = -1;
                for (int i = 0; i < MAX_CONNS; i++) if (conns[i].fd < 0) { slot = i; break; }
                /* her uid only, and only a few of them */
                if (slot < 0 || getsockopt(nfd, SOL_SOCKET, SO_PEERCRED, &uc, &ul) < 0 || uc.uid != getuid()) close(nfd);
                else { conns[slot].fd = nfd; conns[slot].len = 0; conns[slot].hello = 0; conns[slot].gen = ++conn_gen; }
            }
        }
        for (int i = 0; i < MAX_CONNS; i++) {
            conn *c = &conns[i];
            if (c->fd < 0 || !(pf[1 + i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            ssize_t k = read(c->fd, c->buf + c->len, LINE_IN + 1 - c->len);
            if (k <= 0) { if (k < 0 && (errno == EAGAIN || errno == EINTR)) continue; conn_close(c); continue; }
            c->len += (size_t)k;
            for (;;) {
                char *nl = memchr(c->buf, '\n', c->len);
                if (!nl) {
                    if (c->len > LINE_IN) conn_close(c);               /* a line that never ends */
                    break;
                }
                size_t ll = (size_t)(nl - c->buf);
                char line[LINE_IN + 8];
                memcpy(line, c->buf, ll);
                line[ll] = 0;
                memmove(c->buf, nl + 1, c->len - ll - 1);
                c->len -= ll + 1;
                if (ll == 0) continue;
#ifdef AURWISH_TEST
                if (strstr(line, "\"inject\"")) {          /* a fake plugging, for tests */
                    if (strstr(line, "\"usb\"")) engine_usb(E, now);
                    continue;
                }
#endif
                int r = engine_request(E, line, ll, reply, sizeof reply, now);
                if (r < 0) { conn_close(c); break; }
                if (r == 1) cloud_op(E, &ST, c, i, line, ll, now, reply);
                else if (reply[0]) reply_to(c, reply);
                if (c->fd < 0) break;
                if (r == 0 && strstr(reply, "\"kind\":\"hello\"")) {
                    /* hello's reply is out; now the words the compositor may match */
                    c->hello = 1;
                    engine_triggers_line(E, trig, sizeof trig);
                    send_line(c, trig);
                    if (!started) { started = 1; engine_startup(E, now); }
                }
            }
        }

        /* the cloud */
        if (cur.active) {
            cloud_result res;
            if (cloud_step(CL, now, &res)) {
                cur.active = 0;
                answer_reply(E, &res, cur.said, cur.voice, help_dir, reply, sizeof reply, now);
                for (int i = 0; i < MAX_CONNS; i++)
                    if (conns[i].fd >= 0 && conns[i].gen == cur.gen) { reply_to(&conns[i], reply); break; }
            }
        }

        /* USB */
        if (ufd >= 0 && (pf[np - 1].revents & POLLIN)) {
            int n = uevent_read(ufd);
            if (n > 0 && now - last_usb >= 3) { last_usb = now; engine_usb(E, now); }
        }

        /* the clock, and the sources that need a look */
        if (now != last_tick) {
            last_tick = now;
            unsigned want = engine_wants(E);
            if (want != claimed || (want & (WANT_COMPASS | WANT_TILT | WANT_LIGHT))) {
                unsigned have2 = sensors_have(SN, now, 0);
                if (have2 != have) { have = have2; env.sensors = have; cloud_set_sensors(CL, have); engine_sensors(E, have); }
                sensors_want(SN, want, now);
                claimed = want;
                sensor_reading sr;
                sensors_read(SN, now, &sr);
                if (sr.have_heading) engine_compass(E, sr.heading, now);
                if (sr.have_orient) engine_tilt(E, sr.orient, now);
                if (sr.have_lux) engine_light(E, sr.lux, now);
            }
            if ((want & WANT_BATTERY) && now - last_bat >= 10) {
                last_bat = now;
                power_battery fb;
                power_battery_read(&fb);
                engine_battery(E, fb.present, fb.percent, fb.plugged, now);
            }
            engine_tick(E, now);
        }
        power_step();                           /* flushes a volume she just set */
        run_reap();
    }

    sensors_want(SN, 0, (long long)time(NULL));
    unlink(sock);
    cloud_free(CL);
    return 0;
}
