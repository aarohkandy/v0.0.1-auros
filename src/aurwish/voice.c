/* voice.c — aurvoice: her voice in, the computer's voice out.
 * docs/AI.md §4 and §7 change 9 (stream W2).
 *
 *   aurvoice listen [--max SECONDS]
 *       Records from the microphone into MEMORY, until she presses "I've
 *       finished" (the shell sends SIGUSR1), SECONDS pass (never more than
 *       30), or she cancels (SIGTERM: nothing is transcribed, nothing is
 *       printed). Then turns what she said into words on this computer
 *       with whisper.cpp, and prints ONE line:
 *
 *           heard <her words>     it understood
 *           unsure                too quiet, too short, or nothing it can
 *                                 stand behind: she is asked to say it
 *                                 again or type it
 *           nomic                 no microphone it can use
 *           broken                the recogniser did not run
 *
 *   aurvoice say <text>
 *       Says the text aloud, on this computer: piper if it is installed,
 *       espeak-ng if not. The text is an argument, never a shell word.
 *
 * WHAT IT NEVER DOES
 *
 *  - Write her voice anywhere. The recording lives in this process's
 *    memory and in an anonymous memory file (memfd) that the recogniser
 *    reads through /proc/self/fd; neither has a name on any disk. Both are
 *    overwritten and released before this program exits, whether the
 *    transcription worked or not. No core dump (PR_SET_DUMPABLE 0).
 *  - Open the network. The recogniser runs with a model file and nothing
 *    else, and this program has no socket code at all.
 *  - Run anything through a shell.
 *
 * The recogniser and the voices are files the image carries; where they
 * live is a macro so tools/voicetest.sh can point it at stand-ins.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#ifndef AURVOICE_WHISPER
#define AURVOICE_WHISPER "/usr/lib/auros/whisper-cli"
#endif
#ifndef AURVOICE_MODEL
#define AURVOICE_MODEL   "/usr/share/auros/voice/ggml-base.en-q5_1.bin"
#endif
#ifndef AURVOICE_PIPER
#define AURVOICE_PIPER   "/usr/lib/auros/piper/piper"
#endif
#ifndef AURVOICE_VOICE
#define AURVOICE_VOICE   "/usr/share/auros/voice/en_US-lessac-low.onnx"
#endif
#ifndef AURVOICE_ESPEAK
#define AURVOICE_ESPEAK  "espeak-ng"
#endif
/* The recorders, in the order they are tried. Each is an argv ending in
 * NULL; the PCM (or a WAV of it) comes out of stdout. */
#ifndef AURVOICE_RECORDERS
#define AURVOICE_RECORDERS \
    { "parec", "--rate=16000", "--channels=1", "--format=s16le", "--raw", NULL }, \
    { "pw-cat", "--record", "--rate", "16000", "--channels", "1", "--format", "s16", "-", NULL }, \
    { "arecord", "-q", "-t", "raw", "-f", "S16_LE", "-r", "16000", "-c", "1", NULL }
#endif
/* Players for piper's raw output; %RATE% is the voice's sample rate. */
#ifndef AURVOICE_PLAYERS
#define AURVOICE_PLAYERS \
    { "paplay", "--raw", "--rate=%RATE%", "--channels=1", "--format=s16le", NULL }, \
    { "pw-cat", "--playback", "--rate", "%RATE%", "--channels", "1", "--format", "s16", "-", NULL }, \
    { "aplay", "-q", "-t", "raw", "-f", "S16_LE", "-r", "%RATE%", "-c", "1", NULL }
#endif

#define RATE_HZ        16000
#define MAX_SECONDS    30
#define MIN_SAMPLES    (RATE_HZ * 4 / 10)       /* under 0.4 s is not a sentence */
#ifndef AURVOICE_WHISPER_MS
#define AURVOICE_WHISPER_MS 90000               /* a slow machine, but not for ever */
#endif
#define WHISPER_MS     AURVOICE_WHISPER_MS
#define TEXT_MAX       300
#define FIRST_DATA_MS  1500                     /* a recorder that says nothing by then is not there */

static volatile sig_atomic_t g_stop, g_cancel;
static void on_stop(int s)   { (void)s; g_stop = 1; }
static void on_cancel(int s) { (void)s; g_cancel = 1; }

static int64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* ── her recording, in memory only ──────────────────────────────── */

typedef struct { uint8_t *p; size_t n, cap; } pcm_t;

static void pcm_wipe(pcm_t *b)
{
    if (b->p) {
        explicit_bzero(b->p, b->cap);
        munmap(b->p, b->cap);
    }
    b->p = NULL; b->n = b->cap = 0;
}

static int pcm_init(pcm_t *b)
{
    b->cap = (size_t)RATE_HZ * 2 * (MAX_SECONDS + 2) + 4096;
    b->p = mmap(NULL, b->cap, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (b->p == MAP_FAILED) { b->p = NULL; return -1; }
    madvise(b->p, b->cap, MADV_DONTDUMP);
    mlock(b->p, b->cap);                  /* best effort: keeps it out of swap */
    b->n = 0;
    return 0;
}

/* The recorder may hand over a WAV (pw-cat does) or raw samples (parec,
 * arecord). Anything that begins "RIFF" is walked to its data chunk. */
static size_t skip_header(const uint8_t *p, size_t n)
{
    if (n < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) return 0;
    size_t i = 12;
    while (i + 8 <= n) {
        uint32_t len = (uint32_t)p[i + 4] | (uint32_t)p[i + 5] << 8 |
                       (uint32_t)p[i + 6] << 16 | (uint32_t)p[i + 7] << 24;
        if (!memcmp(p + i, "data", 4)) return i + 8;
        if (len > n) return n;
        i += 8 + (size_t)len + (len & 1);
    }
    return n;                              /* header not complete yet: nothing */
}

static void pcm_strip_header(pcm_t *b)
{
    size_t h = skip_header(b->p, b->n);
    if (h && h <= b->n) {
        memmove(b->p, b->p + h, b->n - h);
        b->n -= h;
    }
}

static pid_t spawn_pipe(char *const argv[], int *rfd)
{
    int p[2];
    if (pipe2(p, O_CLOEXEC) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); return -1; }
    if (pid == 0) {
        dup2(p[1], 1);
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { dup2(nul, 0); dup2(nul, 2); }
        execvp(argv[0], argv);
        _exit(127);
    }
    close(p[1]);
    fcntl(p[0], F_SETFL, O_NONBLOCK);
    *rfd = p[0];
    return pid;
}

static void stop_child(pid_t pid)
{
    if (pid <= 0) return;
    kill(pid, SIGTERM);
    for (int i = 0; i < 20; i++) {
        int st;
        if (waitpid(pid, &st, WNOHANG) == pid) return;
        usleep(25000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

/* Record until stopped. Returns 1 with PCM in b, 0 for "no microphone
 * it can use", -1 for cancelled. */
static int record(pcm_t *b, int max_s)
{
    static char *const R[][16] = { AURVOICE_RECORDERS };
    int n_rec = (int)(sizeof R / sizeof R[0]);
    int64_t deadline = 0;
    for (int r = 0; r < n_rec; r++) {
        int fd;
        pid_t pid = spawn_pipe(R[r], &fd);
        if (pid < 0) continue;
        int64_t t0 = now_ms();
        int got_any = 0, alive = 1;
        b->n = 0;
        while (!g_stop && !g_cancel) {
            int64_t t = now_ms();
            if (got_any && t > deadline) break;            /* her limit */
            if (!got_any && t - t0 > FIRST_DATA_MS) break; /* not recording */
            struct pollfd pf = { fd, POLLIN, 0 };
            int pr = poll(&pf, 1, 100);
            if (pr > 0 && (pf.revents & (POLLIN | POLLHUP))) {
                for (;;) {
                    if (b->n >= b->cap) { g_stop = 1; break; }
                    ssize_t k = read(fd, b->p + b->n, b->cap - b->n);
                    if (k > 0) {
                        b->n += (size_t)k;
                        if (!got_any) { got_any = 1; deadline = now_ms() + (int64_t)max_s * 1000; }
                        continue;
                    }
                    if (k == 0) alive = 0;
                    break;
                }
                if (!alive) break;
            }
        }
        close(fd);
        stop_child(pid);
        if (g_cancel) return -1;
        if (got_any) { pcm_strip_header(b); return 1; }
        (void)alive;
    }
    return 0;
}

/* ── is there anything in it ─────────────────────────────────────── */

typedef struct { int peak; double msq; size_t samples; } level_t;   /* msq: mean of the squares */

static level_t level_of(const pcm_t *b)
{
    level_t l = { 0, 0.0, b->n / 2 };
    double acc = 0.0;
    const int16_t *s = (const int16_t *)b->p;
    for (size_t i = 0; i < l.samples; i++) {
        int v = s[i] < 0 ? -(int)s[i] : (int)s[i];
        if (v > l.peak) l.peak = v;
        acc += (double)v * (double)v;
    }
    if (l.samples) l.msq = acc / (double)l.samples;
    return l;
}

/* ── the recogniser ──────────────────────────────────────────────── */

static void put_le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void put_le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* A WAV of the recording in an anonymous memory file. -1 on failure. */
static int wav_memfd(const pcm_t *b)
{
    int fd = (int)syscall(SYS_memfd_create, "aurvoice", 0u);   /* no CLOEXEC: the child reads it */
    if (fd < 0) return -1;
    uint8_t h[44];
    memcpy(h, "RIFF", 4);  put_le32(h + 4, (uint32_t)(36 + b->n));
    memcpy(h + 8, "WAVEfmt ", 8);
    put_le32(h + 16, 16);  put_le16(h + 20, 1);  put_le16(h + 22, 1);
    put_le32(h + 24, RATE_HZ);  put_le32(h + 28, RATE_HZ * 2);
    put_le16(h + 32, 2);   put_le16(h + 34, 16);
    memcpy(h + 36, "data", 4);  put_le32(h + 40, (uint32_t)b->n);
    if (write(fd, h, 44) != 44 || write(fd, b->p, b->n) != (ssize_t)b->n) { close(fd); return -1; }
    lseek(fd, 0, SEEK_SET);
    return fd;
}

static int cpu_threads(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n < 1 ? 1 : (n > 4 ? 4 : (int)n);
}

/* Run the recogniser; its words into out. 0 ok, -1 it did not run. */
static int transcribe(const pcm_t *b, char *out, size_t outn)
{
    out[0] = 0;
    int mfd = wav_memfd(b);
    if (mfd < 0) return -1;
    char path[48], thr[8];
    snprintf(path, sizeof path, "/proc/self/fd/%d", mfd);
    snprintf(thr, sizeof thr, "%d", cpu_threads());
    char *argv[] = { (char *)AURVOICE_WHISPER, "-m", (char *)AURVOICE_MODEL, "-f", path,
                     "-l", "en", "-t", thr, "-nt", "-np", "-ng", NULL };
    int rfd;
    pid_t pid = spawn_pipe(argv, &rfd);
    if (pid < 0) { close(mfd); return -1; }
    size_t n = 0;
    int64_t t0 = now_ms();
    int alive = 1;
    while (alive && !g_cancel) {
        if (now_ms() - t0 > WHISPER_MS) break;
        struct pollfd pf = { rfd, POLLIN, 0 };
        if (poll(&pf, 1, 100) > 0) {
            for (;;) {
                char tmp[512];
                ssize_t k = read(rfd, tmp, sizeof tmp);
                if (k > 0) {
                    for (ssize_t i = 0; i < k && n + 1 < outn; i++) out[n++] = tmp[i];
                    continue;
                }
                if (k == 0) alive = 0;
                break;
            }
        }
    }
    out[n] = 0;
    close(rfd);
    int status = 0;
    pid_t w = waitpid(pid, &status, WNOHANG);
    if (w == 0 && alive) stop_child(pid);
    else if (w == 0) { waitpid(pid, &status, 0); }
    close(mfd);
    if (g_cancel) return -1;
    if (alive) return -1;                       /* timed out */
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) return -1;
    if (WIFSIGNALED(status)) return -1;
    return 0;
}

/* Her words out of what the recogniser printed: lines joined, [BLANK_AUDIO]
 * and (music) and the like dropped, control characters spaces, one space
 * between words, never more than TEXT_MAX bytes and never half a
 * character. */
static void clean_words(const char *in, char *out, size_t outn)
{
    size_t o = 0;
    int space = 1, skip = 0;
    char close_ch = 0;
    for (const char *p = in; *p && o + 1 < outn && o < TEXT_MAX; p++) {
        unsigned char c = (unsigned char)*p;
        if (skip) { if (c == (unsigned char)close_ch) skip = 0; continue; }
        if (c == '[' || c == '(') { skip = 1; close_ch = c == '[' ? ']' : ')'; continue; }
        if (c < 0x20 || c == 0x7f) c = ' ';
        if (c == ' ') { if (!space) out[o++] = ' '; space = 1; continue; }
        out[o++] = (char)c;
        space = 0;
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    /* Not through the middle of a UTF-8 sequence. */
    if (o > 0 && o >= TEXT_MAX) {
        size_t s = o;
        while (s > 0 && ((unsigned char)out[s - 1] & 0xC0) == 0x80) s--;
        if (s > 0 && (unsigned char)out[s - 1] >= 0xC0) o = s - 1;
    }
    out[o] = 0;
}

/* What a speech model says when it was given nothing: it was trained on
 * video, and "thank you" and "you" are what it makes up. Believed only
 * if the room was not silent. */
static int ghost_words(const char *w)
{
    char lo[64];
    size_t o = 0;
    for (; *w && o + 1 < sizeof lo; w++) {
        unsigned char c = (unsigned char)*w;
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
        if ((c >= 'a' && c <= 'z') || c == ' ') lo[o++] = (char)c;
    }
    lo[o] = 0;
    static const char *const G[] = { "you", "thank you", "thanks", "bye", "thanks for watching",
                                     "thank you for watching", "so", "okay", "oh", "uh", "um", NULL };
    for (int i = 0; G[i]; i++) if (!strcmp(lo, G[i])) return 1;
    return 0;
}

static int cmd_listen(int max_s)
{
    if (max_s < 1) max_s = 1;
    if (max_s > MAX_SECONDS) max_s = MAX_SECONDS;
    pcm_t b = { 0 };
    if (pcm_init(&b) < 0) { puts("broken"); return 1; }
    int rc = record(&b, max_s);
    if (rc < 0) { pcm_wipe(&b); return 1; }          /* cancelled: nothing said, nothing kept */
    if (rc == 0) { pcm_wipe(&b); puts("nomic"); return 0; }

    level_t lv = level_of(&b);
    /* Quiet enough to be a muted microphone, or too short to be words. */
    if (lv.samples < MIN_SAMPLES || lv.peak < 300) { pcm_wipe(&b); puts("unsure"); return 0; }

    char raw[4096], words[TEXT_MAX + 8];
    int tr = transcribe(&b, raw, sizeof raw);
    pcm_wipe(&b);
    if (g_cancel) return 1;
    if (tr < 0) { puts("broken"); return 0; }
    clean_words(raw, words, sizeof words);
    if (!words[0] || (lv.msq < 400.0 * 400.0 && ghost_words(words))) { puts("unsure"); return 0; }
    printf("heard %s\n", words);
    return 0;
}

/* ── saying it ───────────────────────────────────────────────────── */

/* The sample rate of a piper voice: "sample_rate": N in its .json. */
static int voice_rate(const char *onnx)
{
    char js[512];
    snprintf(js, sizeof js, "%s.json", onnx);
    FILE *f = fopen(js, "r");
    if (!f) return 22050;
    char buf[8192];
    size_t k = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[k] = 0;
    const char *p = strstr(buf, "\"sample_rate\"");
    if (!p) return 22050;
    p = strchr(p, ':');
    int r = p ? atoi(p + 1) : 0;
    return (r >= 8000 && r <= 48000) ? r : 22050;
}

/* Control characters out, length capped, nothing a command line could
 * take for anything but words. */
static void say_clean(const char *in, char *out, size_t outn)
{
    size_t o = 0;
    int space = 1;
    for (; *in && o + 1 < outn && o < TEXT_MAX; in++) {
        unsigned char c = (unsigned char)*in;
        if (c < 0x20 || c == 0x7f) c = ' ';
        if (c == ' ') { if (!space) out[o++] = ' '; space = 1; continue; }
        out[o++] = (char)c;
        space = 0;
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = 0;
}

static int run_wait(char *const argv[], int stdin_fd, int stdout_fd, int ms)
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (stdin_fd >= 0)  dup2(stdin_fd, 0);
        if (stdout_fd >= 0) dup2(stdout_fd, 1);
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { if (stdin_fd < 0) dup2(nul, 0); if (stdout_fd < 0) dup2(nul, 1); dup2(nul, 2); }
        execvp(argv[0], argv);
        _exit(127);
    }
    int64_t t0 = now_ms();
    for (;;) {
        int st;
        if (waitpid(pid, &st, WNOHANG) == pid)
            return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        if (now_ms() - t0 > ms) { stop_child(pid); return -1; }
        usleep(20000);
    }
}

static int cmd_say(const char *text)
{
    char t[TEXT_MAX + 8];
    say_clean(text, t, sizeof t);
    if (!t[0]) return 0;

    if (access(AURVOICE_PIPER, X_OK) == 0 && access(AURVOICE_VOICE, R_OK) == 0) {
        char rate[16];
        snprintf(rate, sizeof rate, "%d", voice_rate(AURVOICE_VOICE));
        /* piper reads the words on stdin (a pipe, never a command line)
         * and writes raw samples; a player takes them. */
        static char *const P[][16] = { AURVOICE_PLAYERS };
        for (size_t pl = 0; pl < sizeof P / sizeof P[0]; pl++) {
            if (!P[pl][0]) break;
            /* The player's words, with the voice's own rate put in where
             * %RATE% stands -- inside a word ("--rate=%RATE%") or as one. */
            char *pa[16];
            char fixed[8][48];
            int n = 0, fi = 0;
            for (; P[pl][n] && n < 15; n++) {
                const char *tpl = P[pl][n];
                const char *m = strstr(tpl, "%RATE%");
                if (!m) { pa[n] = (char *)tpl; continue; }
                if (fi >= 8) { pa[n] = rate; continue; }
                snprintf(fixed[fi], sizeof fixed[fi], "%.*s%s", (int)(m - tpl), tpl, rate);
                pa[n] = fixed[fi++];
            }
            pa[n] = NULL;
            int tp[2], pp[2];
            if (pipe(tp) < 0) return 1;
            if (pipe(pp) < 0) { close(tp[0]); close(tp[1]); return 1; }
            char *pip[] = { (char *)AURVOICE_PIPER, "--model", (char *)AURVOICE_VOICE,
                            "--output_raw", NULL };
            pid_t pipe_pid = fork();
            if (pipe_pid == 0) {
                dup2(tp[0], 0); dup2(pp[1], 1);
                int nul = open("/dev/null", O_RDWR);
                if (nul >= 0) dup2(nul, 2);
                close(tp[1]); close(pp[0]);
                execvp(pip[0], pip);
                _exit(127);
            }
            pid_t play_pid = fork();
            if (play_pid == 0) {
                dup2(pp[0], 0);
                int nul = open("/dev/null", O_RDWR);
                if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); }
                close(tp[0]); close(tp[1]); close(pp[1]);
                execvp(pa[0], pa);
                _exit(127);
            }
            close(tp[0]); close(pp[0]); close(pp[1]);
            ssize_t w = write(tp[1], t, strlen(t));
            if (w > 0) (void)!write(tp[1], "\n", 1);
            close(tp[1]);
            int st1 = 0, st2 = 0;
            int64_t t0 = now_ms();
            int done1 = 0, done2 = 0;
            while (!(done1 && done2)) {
                if (!done1 && waitpid(pipe_pid, &st1, WNOHANG) == pipe_pid) done1 = 1;
                if (!done2 && waitpid(play_pid, &st2, WNOHANG) == play_pid) done2 = 1;
                if (now_ms() - t0 > 60000) {
                    if (!done1) stop_child(pipe_pid);
                    if (!done2) stop_child(play_pid);
                    break;
                }
                usleep(20000);
            }
            if (done2 && WIFEXITED(st2) && WEXITSTATUS(st2) == 0 &&
                done1 && WIFEXITED(st1) && WEXITSTATUS(st1) == 0) return 0;
            /* That player was not there, or piper failed: try the next. */
            if (done1 && WIFEXITED(st1) && WEXITSTATUS(st1) != 0) break;
        }
    }
    /* espeak-ng: slow and clear. "--" so a sentence that starts with a
     * dash is words. */
    char *es[] = { (char *)AURVOICE_ESPEAK, "-s", "140", "--", t, NULL };
    return run_wait(es, -1, -1, 60000) == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    prctl(PR_SET_DUMPABLE, 0);
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_stop;   sigaction(SIGUSR1, &sa, NULL);
    sa.sa_handler = on_cancel; sigaction(SIGTERM, &sa, NULL);
                               sigaction(SIGINT, &sa, NULL);

    if (argc >= 2 && !strcmp(argv[1], "listen")) {
        int max_s = MAX_SECONDS;
        for (int i = 2; i < argc; i++)
            if (!strcmp(argv[i], "--max") && i + 1 < argc) max_s = atoi(argv[++i]);
        setvbuf(stdout, NULL, _IOLBF, 0);
        return cmd_listen(max_s);
    }
    if (argc >= 3 && !strcmp(argv[1], "say")) return cmd_say(argv[2]);
    fputs("usage: aurvoice listen [--max SECONDS] | aurvoice say TEXT\n", stderr);
    return 2;
}
