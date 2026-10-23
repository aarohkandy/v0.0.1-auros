/* ═══════════════════════════════════════════════════════════════════
 *  ferry-copy — her folders come across: fast, honestly, and as her.
 *
 *  One process for every folder. The shell loop this replaces paid a
 *  dozen forks per file (26 ms each: 5,000 small files took two
 *  minutes before a byte of a photo moved). Here a file costs a few
 *  system calls.
 *
 *  What it promises, in the order they matter:
 *
 *  1. NOTHING ON THE WINDOWS SIDE CHANGES. Sources are only opened
 *     O_RDONLY, under a read-only mount, and links are never followed.
 *  2. ROOT NEVER WRITES INTO HER HOME. Root opens the Windows folders
 *     and Ferry's own state files, then this process becomes her
 *     (setgroups/setresgid/setresuid, checked) before it touches her
 *     home. A symlink she -- or a program running as her -- planted at
 *     ~/Pictures can then only send her own files where she could
 *     already write.
 *  3. NO HALF FILE IS EVER VISIBLE. Each file is written to a hidden
 *     temporary beside its final place; a batch of them is flushed to
 *     the disk (one syncfs) and only then renamed into place with
 *     RENAME_NOREPLACE. A flat battery leaves complete files and
 *     hidden temporaries, which the next run removes and redoes.
 *  4. NOTHING OF HERS IS OVERWRITTEN. If the name is taken by a
 *     different file, Windows' copy lands beside it as
 *     "name (from Windows).ext", then "(from Windows 2)", and so on.
 *     The same file (size and time) is recognised and not copied twice.
 *  5. A FILE THAT IS NOT ON THIS DISK IS NEVER COPIED AS IF IT WERE.
 *     ferry-winattr's rules decide (OneDrive online-only, encrypted,
 *     links); a file the rules call `suspect` is read, and kept only if
 *     a single byte of it is not zero.
 *  6. PROGRESS IS BY BYTES, AND SAID PLAINLY. Every folder is walked
 *     first, so the total is known before the first byte moves; then,
 *     about once a second, one line for her ("Copying your Pictures:
 *     1,200 of 5,000 files. About 4 minutes left.") on FERRY_PLAIN_FD,
 *     and a JSON line in STATE/state/STAGE.progress (its work_* fields
 *     are bytes plus FILE_COST per file: what the bar and the time left
 *     are measured in).
 *
 *    ferry-copy [--as UID:GID] [--state DIR] [--stage NAME] [--dry-run]
 *               [--to-windows] [--skip NAME]... [--skip-dir NAME]...
 *               --job LABEL SRC DEST [--job LABEL SRC DEST]...
 *
 *  --to-windows is the way back ("Put Windows back"): her AurOS files
 *  go onto the Windows volume. Windows is stricter than Linux about
 *  names, so each is made safe (no : * ? " < > | \ or control
 *  characters, no trailing dot or space, no CON/NUL/COM1...), names that
 *  differ only in capitals are kept apart (Windows treats them as one
 *  file), a collision is "(from AurOS)", and before a single byte moves
 *  the free space is checked against what is needed: not enough room
 *  copies nothing and exits 4. Same safety otherwise: as her, no link
 *  followed, nothing overwritten.
 *
 *  Report rows (what did not come, and why) go to
 *  STATE/report.d/STAGE-copy.tsv, rewritten whole each run so a re-run
 *  tells the current truth; copies are appended to STATE/manifest.tsv.
 *
 *  exit 0 everything that could come came · 2 something of hers did
 *  not (it is in the report) · 4 not enough room (nothing copied) ·
 *  1 could not run, or not one file came
 * ═══════════════════════════════════════════════════════════════════ */
#define _GNU_SOURCE
#define WINATTR_NO_MAIN
#include "winattr.c"

#include <ctype.h>
#include <dirent.h>
#include <fnmatch.h>
#include <locale.h>
#include <wctype.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>

#define MAX_JOBS    32
#define MAX_SKIPS   64
#define MAX_DEPTH   128
#define NAME_LIMIT  255                 /* bytes, ext4 and friends      */
#define BUFSZ       (1u << 20)
#define BATCH_FILES 256                 /* renamed after one syncfs     */
#define BATCH_BYTES (64ll << 20)
#define BATCH_NS    2000000000ll
/* What one file costs beyond its bytes (open, create, rename, metadata),
 * in bytes' worth of time. Measured on a laptop-class disk; it is what
 * keeps "4 minutes left" honest for a folder of ten thousand icons. */
#define FILE_COST   (64ll << 10)

struct job {
    const char *label, *src, *dst;
    int srcfd;
    long long copied, already, online, locked, unreadable, failed;
};

static struct job jobs[MAX_JOBS];
static int njobs;
static const char *skip_names[MAX_SKIPS], *skip_dirs[MAX_SKIPS];
static int nskip, nskipd;
static const char *stage = "files";
static int dry_run, to_win;
static long long need_bytes;                 /* --to-windows: space the plan needs */
static int fd_manifest = -1, fd_report = -1, fd_progress = -1, fd_plain = -1;
static int fd_proglog = -1;                 /* FERRY_PROGRESS: every line, appended */
static long long tick_ns = 1000000000ll;    /* FERRY_TICK_MS (tests) */
static unsigned char *buf;
static const char *from_word = "Windows";    /* "(from Windows)" / "(from AurOS)" */

/* Counting. disp_* is what she is told ("1,200 of 5,000 files");
 * work_* drives the bar and the time left, and leaves out files that
 * were already here, so a resumed run does not promise a speed that
 * was only ever skipping. */
static long long disp_total, disp_done;
static long long work_total, work_done;
static long long t_start, t_last;
static const char *cur_label = "";
static int missing_any, out_of_room;
static unsigned long seq;

static long long now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000ll + ts.tv_nsec;
}

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("ferry-copy: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

static int ieq_name(const char *a, const char *b) { return strcasecmp(a, b) == 0; }

static int in_list(const char *name, const char **list, int n)
{
    for (int i = 0; i < n; i++)
        if (ieq_name(name, list[i]) || fnmatch(list[i], name, FNM_CASEFOLD) == 0) return 1;
    return 0;
}

/* What Windows scatters through folders that was never hers. */
static int is_junk(const char *name)
{
    static const char *junk[] = { "desktop.ini", "Thumbs.db", "ehthumbs.db",
                                  ".DS_Store" };
    return in_list(name, junk, (int)(sizeof junk / sizeof junk[0]));
}

/* ── names ─────────────────────────────────────────────────────────
 * The name a file gets at attempt n: n = 0 its own, 1 "(from Windows)",
 * n "(from Windows n)". Always fits in NAME_LIMIT bytes: NTFS allows 255
 * UTF-16 units, which is up to 765 UTF-8 bytes, and ext4 allows 255. A
 * name that does not fit is shortened at a character boundary, keeping
 * a short extension. Returns 1 when it had to shorten. */
static int make_name(const char *orig, int n, char out[NAME_LIMIT + 1])
{
    size_t len = strlen(orig);
    const char *dot = strrchr(orig, '.');
    size_t stem = len;
    if (dot && dot != orig && strlen(dot) <= 32) stem = (size_t)(dot - orig);
    size_t extlen = len - stem;
    char suf[40] = "";
    if (n == 1) snprintf(suf, sizeof suf, " (from %s)", from_word);
    else if (n > 1) snprintf(suf, sizeof suf, " (from %s %d)", from_word, n);
    size_t suflen = strlen(suf);
    size_t room = NAME_LIMIT - extlen - suflen;
    int shortened = 0;
    if (stem > room) {
        size_t cut = room;
        while (cut > 0 && ((unsigned char)orig[cut] & 0xC0) == 0x80) cut--;
        /* Bytes that are all continuation bytes are not text and have no
         * character boundary to cut at (found by the fuzzer: the name came
         * out empty). Cut them where they stand rather than lose the file. */
        stem = cut > 0 ? cut : room;
        shortened = 1;
    }
    memcpy(out, orig, stem);
    memcpy(out + stem, suf, suflen);
    memcpy(out + stem + suflen, orig + len - extlen, extlen);
    out[stem + suflen + extlen] = '\0';
    return shortened;
}

/* ── Windows names ─────────────────────────────────────────────────
 * What Linux allows in a name and Windows does not. Returns 1 when the
 * name had to change. Bytes that are not UTF-8 (Linux allows any) would
 * make the NTFS driver refuse the file, so each becomes "_" too. */
static int win_safe(const char *in, char out[NAME_LIMIT + 1])
{
    size_t o = 0, i = 0, n = strlen(in);
    int changed = 0;
    while (i < n && o < NAME_LIMIT) {
        unsigned char c = (unsigned char)in[i];
        size_t k = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2
                 : (c & 0xF8) == 0xF0 ? 3 : 9;
        int good = k != 9 && i + k < n + (k == 0);
        for (size_t j = 1; good && j <= k; j++)
            if (((unsigned char)in[i + j] & 0xC0) != 0x80) good = 0;
        if (good && c >= 0x20 && !strchr("<>:\"/\\|?*", c) && !(c == 0 )) {
            if (o + k + 1 > NAME_LIMIT) break;
            memcpy(out + o, in + i, k + 1); o += k + 1; i += k + 1;
        } else {
            out[o++] = '_'; i += good ? k + 1 : 1; changed = 1;
        }
    }
    if (i < n) changed = 1;                 /* cut at the limit; make_name shortens properly */
    while (o > 0 && (out[o - 1] == '.' || out[o - 1] == ' ')) { out[o - 1] = '_'; changed = 1; }
    if (o == 0) { out[o++] = '_'; changed = 1; }
    out[o] = '\0';
    {   /* CON, PRN, AUX, NUL, COM1-9, LPT1-9, with or without an extension */
        char stem[8]; size_t sl = 0;
        while (out[sl] && out[sl] != '.' && sl < 7) { stem[sl] = (char)toupper((unsigned char)out[sl]); sl++; }
        stem[sl] = '\0';
        if (out[sl] == '\0' || out[sl] == '.') {
            int bad = !strcmp(stem, "CON") || !strcmp(stem, "PRN") || !strcmp(stem, "AUX") ||
                      !strcmp(stem, "NUL") ||
                      ((!strncmp(stem, "COM", 3) || !strncmp(stem, "LPT", 3)) && sl == 4 &&
                       stem[3] >= '1' && stem[3] <= '9');
            if (bad && o + 1 <= NAME_LIMIT) { memmove(out + 1, out, o + 1); out[0] = '_'; changed = 1; }
        }
    }
    return changed;
}

/* Windows treats Report.docx and report.docx as one file, so on the way
 * back the names already in a folder are remembered by their lower-case
 * form. A small hash table; folding is by character, not byte, so an
 * accented capital meets its small letter too. */
struct ent { char *low, *orig; };
struct nreg { struct ent *t; size_t cap, n; };

static void fold(const char *s, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; s[i] && o + 5 < cap;) {
        unsigned char c = (unsigned char)s[i];
        size_t k = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 9;
        if (k == 0 || k == 9) { out[o++] = (char)tolower(c); i++; continue; }
        wchar_t w = c & (0x3F >> k);
        size_t j;
        for (j = 1; j <= k && s[i + j]; j++) w = (w << 6) | ((unsigned char)s[i + j] & 0x3F);
        if (j <= k) { out[o++] = (char)c; i++; continue; }
        w = (wchar_t)towlower((wint_t)w);
        if (w < 0x800) { out[o++] = (char)(0xC0 | (w >> 6)); out[o++] = (char)(0x80 | (w & 0x3F)); }
        else if (w < 0x10000) { out[o++] = (char)(0xE0 | (w >> 12)); out[o++] = (char)(0x80 | ((w >> 6) & 0x3F));
                                out[o++] = (char)(0x80 | (w & 0x3F)); }
        else { out[o++] = (char)(0xF0 | (w >> 18)); out[o++] = (char)(0x80 | ((w >> 12) & 0x3F));
               out[o++] = (char)(0x80 | ((w >> 6) & 0x3F)); out[o++] = (char)(0x80 | (w & 0x3F)); }
        i += k + 1;
    }
    out[o] = '\0';
}
static uint32_t fnv(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}
static const char *nreg_get(struct nreg *r, const char *low)
{
    if (!r->cap) return NULL;
    for (size_t i = fnv(low) & (r->cap - 1); r->t[i].low; i = (i + 1) & (r->cap - 1))
        if (!strcmp(r->t[i].low, low)) return r->t[i].orig;
    return NULL;
}
static void nreg_put(struct nreg *r, const char *low, const char *orig)
{
    if (r->n * 2 >= r->cap) {
        struct ent *old = r->t; size_t oc = r->cap;
        r->cap = oc ? oc * 2 : 64;
        if (!(r->t = calloc(r->cap, sizeof *r->t))) die("out of memory");
        r->n = 0;
        for (size_t i = 0; i < oc; i++) if (old[i].low) nreg_put(r, old[i].low, old[i].orig), free(old[i].low), free(old[i].orig);
        free(old);
    }
    size_t i = fnv(low) & (r->cap - 1);
    while (r->t[i].low) i = (i + 1) & (r->cap - 1);
    if (!(r->t[i].low = strdup(low)) || !(r->t[i].orig = strdup(orig))) die("out of memory");
    r->n++;
}
static void nreg_free(struct nreg *r)
{
    for (size_t i = 0; i < r->cap; i++) if (r->t[i].low) { free(r->t[i].low); free(r->t[i].orig); }
    free(r->t); memset(r, 0, sizeof *r);
}
static void nreg_load(struct nreg *r, int dfd)
{
    int c = fcntl(dfd, F_DUPFD_CLOEXEC, 0);
    DIR *d = c >= 0 ? fdopendir(c) : NULL;
    struct dirent *e;
    char low[NAME_LIMIT * 4 + 8];
    if (!d) { if (c >= 0) close(c); return; }
    rewinddir(d);
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") ||
            !strncmp(e->d_name, ".ferrypart-", 11)) continue;
        fold(e->d_name, low, sizeof low);
        nreg_put(r, low, e->d_name);
    }
    closedir(d);
}

/* The ledgers */

/* A field for a TSV row: tabs and newlines in a filename would forge
 * a column or a row, so they become spaces (as the shell's note() does). */
static void flat(char *dst, size_t n, const char *src)
{
    size_t i;
    for (i = 0; src[i] && i + 1 < n; i++)
        dst[i] = (src[i] == '\t' || src[i] == '\n' || src[i] == '\r') ? ' ' : src[i];
    dst[i] = '\0';
}

static void note(const char *sev, const struct job *j, const char *rel,
                 const char *detail)
{
    char item[PATH_MAX], msg[NAME_LIMIT * 2 + 400];
    if (fd_report < 0) return;
    flat(item, sizeof item, rel);
    /* The words were written for the way in; on the way back the same
     * two places swap. */
    snprintf(msg, sizeof msg, "%s", detail);
    if (to_win) {
        static const char *pair[][2] = { { "from the Windows drive", "from this computer" },
                                         { "on this computer", "in Windows" } };
        for (int i = 0; i < 2; i++) {
            char *at = strstr(msg, pair[i][0]);
            if (at) {
                char tail[sizeof msg];
                snprintf(tail, sizeof tail, "%s", at + strlen(pair[i][0]));
                snprintf(at, sizeof msg - (size_t)(at - msg), "%s%s", pair[i][1], tail);
            }
        }
    }
    dprintf(fd_report, "%s\t%s\t%s%s%s\t%s\n", stage, sev, j->label,
            *item ? "/" : "", item, msg);
}

static void stamp(char *out, size_t n)
{
    time_t t = time(NULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

static void record(const char *action, long long size, const struct job *j,
                   const char *rel, const char *dstrel)
{
    char at[32], s[PATH_MAX], d[PATH_MAX];
    if (fd_manifest < 0) return;
    stamp(at, sizeof at);
    flat(s, sizeof s, rel);
    flat(d, sizeof d, dstrel);
    dprintf(fd_manifest, "%s\t%s\t%s\t%lld\t%s/%s\t%s/%s\n", at, stage, action,
            size, j->src, s, j->dst, d);
}

/* ── what she sees ───────────────────────────────────────────────── */

static void count_words(long long v, char *out, size_t n)
{
    char raw[32];
    int len = snprintf(raw, sizeof raw, "%lld", v), o = 0;
    for (int i = 0; i < len && (size_t)o + 2 < n; i++) {
        if (i && (len - i) % 3 == 0) out[o++] = ',';
        out[o++] = raw[i];
    }
    out[o] = '\0';
}

static void time_left(long long now, char *out, size_t n)
{
    long long el = now - t_start;
    out[0] = '\0';
    if (work_done <= 0 || el < 3000000000ll || work_done >= work_total) return;
    double rem = (double)el / 1e9 * (double)(work_total - work_done) / (double)work_done;
    if (rem < 60)              snprintf(out, n, " Less than a minute left.");
    else if (rem < 90)         snprintf(out, n, " About a minute left.");
    else if (rem < 90 * 60)    snprintf(out, n, " About %d minutes left.", (int)((rem + 30) / 60));
    else if (rem < 90 * 60 * 2) snprintf(out, n, " About an hour left.");
    else                       snprintf(out, n, " About %d hours left.", (int)((rem + 1800) / 3600));
}

static void tick(int force)
{
    long long now = now_ns();
    if (!force && now - t_last < tick_ns) return;
    t_last = now;

    char a[32], b[32], eta[64], line[256];
    count_words(disp_done, a, sizeof a);
    count_words(disp_total, b, sizeof b);
    time_left(now, eta, sizeof eta);
    if (to_win)
        snprintf(line, sizeof line, "Saving your %s in Windows: %s of %s files.%s",
                 cur_label, a, b, eta);
    else
        snprintf(line, sizeof line, "Copying your %s: %s of %s files.%s",
                 cur_label, a, b, eta);
    int pct = work_total > 0 ? (int)(work_done * 100 / work_total) : 100;
    if (pct > 100) pct = 100;
    if (disp_done >= disp_total) pct = 100;

    if (fd_plain >= 0) dprintf(fd_plain, "%s\n", line);
    if (fd_progress >= 0) {
        char json[640], at[32];
        stamp(at, sizeof at);
        int k = snprintf(json, sizeof json,
            "{\"type\":\"progress\",\"stage\":\"%s\",\"pct\":%d,\"done\":%lld,"
            "\"total\":%lld,\"work_done\":%lld,\"work_total\":%lld,"
            "\"message\":\"%s\",\"at\":\"%s\"}\n",
            stage, pct, disp_done, disp_total, work_done, work_total, line, at);
        if (k > 0 && ftruncate(fd_progress, 0) == 0)
            if (pwrite(fd_progress, json, (size_t)k, 0) < 0) { /* best effort */ }
        if (k > 0 && fd_proglog >= 0 && write(fd_proglog, json, (size_t)k) < 0) { /* best effort */ }
    }
}

/* Where a file went, when it is not where she would look first. Said
 * again on every run, so a re-run's report still tells her. */
static void placed_note(const struct job *j, const char *rel, const char *cand,
                        int n, int shortened)
{
    char msg[NAME_LIMIT + 200];
    if (n > 0)
        snprintf(msg, sizeof msg, "A different file with this name was already "
                 "here, so this one is beside it as \"%s\"%s.", cand,
                 shortened ? " (a shorter name: the full one was too long for AurOS)" : "");
    else if (shortened)
        snprintf(msg, sizeof msg, "It came across with a shorter name, \"%s\": "
                 "its name was too long for AurOS.", cand);
    else
        return;
    note("partial", j, rel, msg);
}

/* ── putting files in place ─────────────────────────────────────── */

struct pend { int dfd; dev_t dev; char tmp[48]; char *name, *rel, *want; long long size; struct job *j; };
static struct pend pend[BATCH_FILES];
static int npend;
static long long pend_bytes, last_commit;

static void commit(void)
{
    dev_t synced[4];
    int nsynced = 0;
    for (int i = 0; i < npend; i++) {
        int seen = 0;
        for (int k = 0; k < nsynced; k++) if (synced[k] == pend[i].dev) seen = 1;
        if (seen) continue;
        syncfs(pend[i].dfd);
        if (nsynced < 4) synced[nsynced++] = pend[i].dev;
    }
    for (int i = 0; i < npend; i++) {
        struct pend *p = &pend[i];
        char cand[NAME_LIMIT + 1];
        int placed = -1, shortened = 0;
        if (p->want) {
            /* On the way back the final name was chosen, case-blind, when
             * the file was copied; it only has to be put there. */
            snprintf(cand, sizeof cand, "%s", p->want);
            if (renameat2(p->dfd, p->tmp, p->dfd, cand, RENAME_NOREPLACE) == 0 ||
                ((errno == EINVAL || errno == ENOSYS) &&
                 linkat(p->dfd, p->tmp, p->dfd, cand, 0) == 0 && unlinkat(p->dfd, p->tmp, 0) == 0))
                placed = 0;
        }
        for (int n = 0; !p->want && n < 100 && placed < 0; n++) {
            shortened = make_name(p->name, n, cand);
            if (renameat2(p->dfd, p->tmp, p->dfd, cand, RENAME_NOREPLACE) == 0) { placed = n; break; }
            if (errno == EINVAL || errno == ENOSYS) {      /* no NOREPLACE here */
                if (linkat(p->dfd, p->tmp, p->dfd, cand, 0) == 0) {
                    unlinkat(p->dfd, p->tmp, 0);
                    placed = n;
                    break;
                }
            }
            if (errno != EEXIST) break;
        }
        if (placed < 0) {
            unlinkat(p->dfd, p->tmp, 0);
            note("blocked", p->j, p->rel, "It could not be saved on this computer.");
            p->j->failed++;
            missing_any = 1;
        } else {
            placed_note(p->j, p->rel, cand, placed, shortened);
            char *slash = strrchr(p->rel, '/');
            char dstrel[PATH_MAX];
            snprintf(dstrel, sizeof dstrel, "%.*s%s", slash ? (int)(slash - p->rel + 1) : 0,
                     p->rel, cand);
            record(placed > 0 ? "collision" : "copy", p->size, p->j, p->rel, dstrel);
            p->j->copied++;
        }
        close(p->dfd);
        free(p->name);
        free(p->rel);
        free(p->want);
    }
    npend = 0;
    pend_bytes = 0;
    last_commit = now_ns();
}

static void pend_add(int dfd, const char *tmp, const char *name, const char *want,
                     const char *rel, long long size, struct job *j)
{
    struct pend *p = &pend[npend];
    struct stat st;
    p->dfd = fcntl(dfd, F_DUPFD_CLOEXEC, 0);
    p->dev = fstat(dfd, &st) == 0 ? st.st_dev : 0;
    snprintf(p->tmp, sizeof p->tmp, "%s", tmp);
    p->name = strdup(name);
    p->rel = strdup(rel);
    p->want = want ? strdup(want) : NULL;
    p->size = size;
    p->j = j;
    if (p->dfd < 0 || !p->name || !p->rel) die("out of memory or descriptors");
    npend++;
    pend_bytes += size;
    if (npend == BATCH_FILES || pend_bytes >= BATCH_BYTES ||
        now_ns() - last_commit >= BATCH_NS)
        commit();
}

static int write_all(int fd, const unsigned char *p, size_t n)
{
    while (n) {
        ssize_t k = write(fd, p, n);
        if (k < 0) { if (errno == EINTR) continue; return -1; }
        p += k; n -= (size_t)k;
    }
    return 0;
}

static int all_zero(const unsigned char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) if (p[i]) return 0;
    return 1;
}

/* The same file, for "have I already brought this across?": size and
 * modification time (to two seconds, as FAT-era tools round). */
static int same(const struct stat *a, const struct stat *b)
{
    long long d = (long long)a->st_mtime - (long long)b->st_mtime;
    return S_ISREG(b->st_mode) && a->st_size == b->st_size && d >= -2 && d <= 2;
}

/* The name this file will have in Windows, chosen now and case-blind:
 * its own made safe for Windows, then "(from AurOS)", "(from AurOS 2)"...
 * until one is free (or is already this very file). Returns 1 when the
 * file is already there. `final` receives the name. */
static int pick_windows_name(struct nreg *R, int dfd, const char *name, const struct stat *st,
                             const struct job *j, const char *rel, char final[NAME_LIMIT + 1])
{
    char safe[NAME_LIMIT + 1], low[NAME_LIMIT * 4 + 8];
    int renamed = win_safe(name, safe);
    for (int n = 0; n < 1000; n++) {
        int shortened = make_name(safe, n, final);
        const char *have;
        fold(final, low, sizeof low);
        have = nreg_get(R, low);
        if (have) {
            struct stat dst;
            if (fstatat(dfd, have, &dst, AT_SYMLINK_NOFOLLOW) == 0 && same(st, &dst)) {
                snprintf(final, NAME_LIMIT + 1, "%s", have);
                return 1;
            }
            continue;
        }
        nreg_put(R, low, final);
        if (renamed || shortened || n > 0) {
            char msg[NAME_LIMIT + 200];
            if (n > 0)
                snprintf(msg, sizeof msg, "A different file with this name was already in Windows "
                         "(Windows treats big and small letters as the same), so this one is saved "
                         "beside it as \"%s\".", final);
            else
                snprintf(msg, sizeof msg, "Windows cannot hold some of the letters in this name, "
                         "so it was saved as \"%s\".", final);
            note("partial", j, rel, msg);
        }
        return 0;
    }
    final[0] = '\0';
    return 0;
}

static void copy_one(struct job *j, int sfd, int dfd, const char *name,
                     const struct stat *st, int suspect, const char *rel, struct nreg *R)
{
    char cand[NAME_LIMIT + 1], tmp[48], want[NAME_LIMIT + 1];
    struct stat dst;
    int have_want = 0;

    if (to_win) {
        if (pick_windows_name(R, dfd, name, st, j, rel, want)) {
            j->already++;
            disp_done++;
            work_total -= st->st_size + FILE_COST;
            tick(0);
            return;
        }
        if (!want[0]) { note("blocked", j, rel, "It could not be saved on this computer."); j->failed++; missing_any = 1; disp_done++; return; }
        have_want = 1;
    } else
    /* Already here, under its own name or a "(from Windows)" one? */
    for (int n = 0; n < 100; n++) {
        int shortened = make_name(name, n, cand);
        if (fstatat(dfd, cand, &dst, AT_SYMLINK_NOFOLLOW) != 0) break;
        if (same(st, &dst)) {
            placed_note(j, rel, cand, n, shortened);
            j->already++;
            disp_done++;
            work_total -= st->st_size + FILE_COST;
            tick(0);
            return;
        }
    }

    long long got = 0;
    int nonzero = 0, sf, df = -1;
    sf = openat(sfd, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    if (sf < 0) goto unreadable;
    posix_fadvise(sf, 0, 0, POSIX_FADV_SEQUENTIAL);
    snprintf(tmp, sizeof tmp, ".ferrypart-%ld-%lu", (long)getpid(), seq++);
    df = openat(dfd, tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (df < 0) goto unwritable;

    for (;;) {
        ssize_t k = read(sf, buf, BUFSZ);
        if (k < 0) { if (errno == EINTR) continue; goto unreadable; }
        if (k == 0) break;
        if (suspect && !nonzero) nonzero = !all_zero(buf, (size_t)k);
        if (write_all(df, buf, (size_t)k) < 0) goto unwritable;
        got += k;
        work_done += k;
        tick(0);
    }
    if (got != st->st_size) goto unreadable;   /* it changed, or a short read */
    if (suspect && !nonzero) {
        /* Size but no blocks, and nothing but zeros: the bytes were never
         * on this disk. A file of zeros named holiday.jpg is the lie this
         * program exists not to tell. */
        unlinkat(dfd, tmp, 0);
        close(df);
        close(sf);
        note("skipped", j, rel, "Windows lists it, but what is in it is not on this "
             "computer (OneDrive may keep it online), so it could not come across.");
        j->online++;
        missing_any = 1;
        goto done;
    }
    struct timespec ts[2] = { st->st_atim, st->st_mtim };
    futimens(df, ts);
    if (close(df) != 0) { df = -1; goto unwritable; }
    close(sf);
    work_done += FILE_COST;
    pend_add(dfd, tmp, name, have_want ? want : NULL, rel, st->st_size, j);
    goto done;

unwritable:
    if (errno == ENOSPC || errno == EDQUOT) out_of_room = 1;
    if (df >= 0) close(df);
    unlinkat(dfd, tmp, 0);
    if (sf >= 0) close(sf);
    if (!out_of_room) note("blocked", j, rel, "It could not be saved on this computer.");
    j->failed++;
    missing_any = 1;
    goto done;
unreadable:
    if (df >= 0) { close(df); unlinkat(dfd, tmp, 0); }
    if (sf >= 0) close(sf);
    note("blocked", j, rel, "It could not be read from the Windows drive.");
    j->unreadable++;
    missing_any = 1;
done:
    disp_done++;
    if (got < st->st_size) work_done += st->st_size - got;
    tick(0);
}

/* Temporaries left by a run that was cut off: never visible files,
 * always safe to remove, and removed before anything new is written. */
static void clean_temps(int fd)
{
    int c = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    DIR *d = c >= 0 ? fdopendir(c) : NULL;
    struct dirent *e;
    if (!d) { if (c >= 0) close(c); return; }
    rewinddir(d);                   /* a dup shares the offset */
    while ((e = readdir(d)))
        if (!strncmp(e->d_name, ".ferrypart-", 11))
            unlinkat(fd, e->d_name, 0);
    closedir(d);
}

/* A folder on her side, made as her. A file already using the name gets
 * the folder beside it, "(from Windows)". */
static int dest_dir(int dfd, const char *name, char *used, struct nreg *R)
{
    char safe[NAME_LIMIT + 1], low[NAME_LIMIT * 4 + 8];
    if (to_win) win_safe(name, safe); else snprintf(safe, sizeof safe, "%s", name);
    for (int n = 0; n < 100; n++) {
        make_name(safe, n, used);
        if (to_win) {
            /* Case-blind: a folder that is there under other capitals is
             * the same folder to Windows. */
            const char *have;
            fold(used, low, sizeof low);
            have = nreg_get(R, low);
            if (have) snprintf(used, NAME_LIMIT + 1, "%s", have);
            else nreg_put(R, low, used);
        }
        int fd = openat(dfd, used, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd < 0 && errno == ENOENT) {
            if (mkdirat(dfd, used, 0755) != 0 && errno != EEXIST) return -1;
            fd = openat(dfd, used, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (fd >= 0) return fd;
        }
        if (fd >= 0) { clean_temps(fd); return fd; }
        if (errno != ENOTDIR) return -1;
    }
    return -1;
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* One pass over a folder. `copy` 0 plans (totals, and the report of
 * what cannot come), 1 copies. Both passes see the same tree in the same
 * order: the volume is read-only. */
static void walk(struct job *j, int sfd, int dfd, char *rel, size_t rlen,
                 int depth, int copy)
{
    int c = fcntl(sfd, F_DUPFD_CLOEXEC, 0);
    DIR *d = c >= 0 ? fdopendir(c) : NULL;
    char **names = NULL;
    size_t n = 0, cap = 0;
    struct dirent *e;

    if (!d) {
        if (c >= 0) close(c);
        if (!copy) { note("blocked", j, rel, "This folder could not be read from the Windows drive."); missing_any = 1; }
        return;
    }
    rewinddir(d);           /* the dup shares sfd's offset: the plan pass left it at the end */
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            names = realloc(names, cap * sizeof *names);
            if (!names) die("out of memory");
        }
        if (!(names[n++] = strdup(e->d_name))) die("out of memory");
    }
    closedir(d);
    qsort(names, n, sizeof *names, by_name);
    struct nreg R = { 0 };
    if (copy && to_win && dfd >= 0) nreg_load(&R, dfd);

    for (size_t i = 0; i < n; i++) {
        const char *name = names[i];
        size_t nl = strlen(name);
        struct stat st;
        if (rlen + nl + 2 >= PATH_MAX) continue;            /* cannot happen on NTFS */
        if (rlen) rel[rlen] = '/';
        memcpy(rel + rlen + (rlen ? 1 : 0), name, nl + 1);
        size_t sub = rlen + (rlen ? 1 : 0) + nl;

        if (fstatat(sfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            if (!copy) { note("blocked", j, rel, "It could not be read from the Windows drive."); missing_any = 1; j->unreadable++; }
            goto next;
        }
        if (S_ISDIR(st.st_mode)) {
            if (in_list(name, skip_dirs, nskipd)) goto next;
            if (depth >= MAX_DEPTH) {
                if (!copy) { note("blocked", j, rel, "It is inside too many folders to bring across."); missing_any = 1; }
                goto next;
            }
            int cs = openat(sfd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (cs < 0) {
                if (!copy) { note("blocked", j, rel, "This folder could not be read from the Windows drive."); missing_any = 1; }
                goto next;
            }
            int cd = -1;
            if (copy && !dry_run) {
                char used[NAME_LIMIT + 1];
                cd = dest_dir(dfd, name, used, &R);
                if (cd < 0) {
                    note("blocked", j, rel, "This folder could not be made on this computer.");
                    missing_any = 1;
                    if (errno == ENOSPC || errno == EDQUOT) out_of_room = 1;
                    close(cs);
                    goto next;
                }
            }
            walk(j, cs, cd, rel, sub, depth + 1, copy);
            close(cs);
            if (cd >= 0) close(cd);
            if (out_of_room) break;
            goto next;
        }

        if (is_junk(name) || in_list(name, skip_names, nskip)) goto next;

        {
            char xp[NAME_LIMIT + 48], key[PATH_MAX * 2];
            uint32_t attrib = 0, tag = 0;
            const char *cls;
            if (to_win) {
                /* Her AurOS files are ordinary files; the NTFS rules (cloud
                 * placeholders, reparse points) do not apply to this side.
                 * A link is not a file Windows can hold, and is never
                 * followed. */
                cls = S_ISREG(st.st_mode) ? "hydrated" : S_ISLNK(st.st_mode) ? "link" : "special";
            } else {
                snprintf(xp, sizeof xp, "/proc/self/fd/%d/%s", sfd, name);
                if (map_path) snprintf(key, sizeof key, "%s/%s", j->src, rel);
                int have = winattr_read(xp, map_path ? key : xp, &attrib, &tag);
                cls = classify(&st, attrib, tag, have);
            }
            int data = !strcmp(cls, "hydrated") || !strcmp(cls, "suspect");

            if (!copy) {
                if (data) {
                    disp_total++;
                    work_total += st.st_size + FILE_COST;
                    /* NTFS keeps a file in whole clusters (4 KiB) plus a
                     * record of its own; this is what the room check weighs. */
                    need_bytes += ((long long)st.st_size + 4095) / 4096 * 4096 + 1024;
                } else if (to_win && !strcmp(cls, "link")) {
                    note("skipped", j, rel, "It is a shortcut to another place, which Windows "
                         "cannot hold. Nothing of yours is in it.");
                } else if (!strcmp(cls, "placeholder")) {
                    note("skipped", j, rel, "It is kept online by OneDrive, not on this "
                         "computer, so it could not come across. It is still in your OneDrive.");
                    j->online++;
                    missing_any = 1;
                } else if (!strcmp(cls, "encrypted")) {
                    note("blocked", j, rel, "It is locked with Windows' own encryption, "
                         "so only Windows can open it.");
                    j->locked++;
                    missing_any = 1;
                } else if (!strcmp(cls, "unreadable")) {
                    note("blocked", j, rel, "It could not be read from the Windows drive.");
                    j->unreadable++;
                    missing_any = 1;
                }
                /* link, reparse (an alias), special: not a file of hers. */
            } else if (data) {
                cur_label = j->label;
                if (dry_run) {
                    disp_done++;
                    work_done += st.st_size + FILE_COST;
                    tick(0);
                } else {
                    copy_one(j, sfd, dfd, name, &st, !strcmp(cls, "suspect"), rel, &R);
                    if (out_of_room) { rel[rlen] = '\0'; break; }
                }
            }
        }
next:
        rel[rlen] = '\0';
    }
    for (size_t i = 0; i < n; i++) free(names[i]);
    free(names);
    nreg_free(&R);
}

static int mkdir_p(const char *path)
{
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s; s++) {
        if (*s != '/') continue;
        *s = '\0';
        if (mkdir(p, 0755) != 0 && errno != EEXIST) return -1;
        *s = '/';
    }
    return mkdir(p, 0755) != 0 && errno != EEXIST ? -1 : 0;
}

static int open_state(const char *dir, const char *sub, const char *name, int flags)
{
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/%s", dir, sub);
    mkdir_p(p);
    snprintf(p, sizeof p, "%s/%s/%s", dir, sub, name);
    return open(p, flags | O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644);
}

int main(int argc, char **argv)
{
    const char *state = NULL, *as = NULL;
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--job") && a + 3 < argc) {
            if (njobs == MAX_JOBS) die("too many jobs");
            jobs[njobs].label = argv[a + 1];
            jobs[njobs].src = argv[a + 2];
            jobs[njobs].dst = argv[a + 3];
            njobs++;
            a += 3;
        } else if (!strcmp(argv[a], "--as") && a + 1 < argc)       as = argv[++a];
        else if (!strcmp(argv[a], "--state") && a + 1 < argc)      state = argv[++a];
        else if (!strcmp(argv[a], "--stage") && a + 1 < argc)      stage = argv[++a];
        else if (!strcmp(argv[a], "--skip") && a + 1 < argc && nskip < MAX_SKIPS)
            skip_names[nskip++] = argv[++a];
        else if (!strcmp(argv[a], "--skip-dir") && a + 1 < argc && nskipd < MAX_SKIPS)
            skip_dirs[nskipd++] = argv[++a];
        else if (!strcmp(argv[a], "--dry-run")) dry_run = 1;
        else if (!strcmp(argv[a], "--to-windows")) { to_win = 1; from_word = "AurOS"; }
        else die("usage: ferry-copy [--as UID:GID] [--state DIR] [--stage NAME] "
                 "[--dry-run] [--to-windows] [--skip NAME]... [--skip-dir NAME]... "
                 "--job LABEL SRC DEST...");
    }
    if (!njobs) die("nothing to do (no --job)");
    if (to_win && !setlocale(LC_CTYPE, "C.UTF-8")) setlocale(LC_CTYPE, "en_US.UTF-8");
    map_path = getenv("FERRY_WINATTR_MAP");
    if (map_path && !*map_path) map_path = NULL;

    /* As root: the Windows folders and Ferry's own records. Nothing in
     * her home is opened before the switch below. */
    for (int i = 0; i < njobs; i++) {
        jobs[i].srcfd = open(jobs[i].src, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (jobs[i].srcfd < 0)
            fprintf(stderr, "ferry-copy: cannot open %s: %s\n", jobs[i].src, strerror(errno));
    }
    if (state) {
        char nm[128];
        fd_manifest = open_state(state, ".", "manifest.tsv", O_APPEND);
        snprintf(nm, sizeof nm, "%s-copy.tsv", stage);
        fd_report = open_state(state, "report.d", nm, O_TRUNC);
        snprintf(nm, sizeof nm, "%s.progress", stage);
        fd_progress = open_state(state, "state", nm, 0);
        if (fd_manifest < 0 || fd_report < 0 || fd_progress < 0)
            die("cannot write Ferry's records in %s: %s", state, strerror(errno));
    }
    const char *plog = getenv("FERRY_PROGRESS"), *tms = getenv("FERRY_TICK_MS");
    if (plog && *plog && fd_progress >= 0)
        fd_proglog = open(plog, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (tms && *tms) tick_ns = atoll(tms) * 1000000ll;
    const char *pfd = getenv("FERRY_PLAIN_FD");
    if (pfd && *pfd) {
        fd_plain = atoi(pfd);
        if (fcntl(fd_plain, F_GETFD) < 0) fd_plain = -1;
    }

    /* Become her. Checked, not assumed: a setuid that failed silently
     * would leave root writing into her home, which is the bug. */
    if (as) {
        unsigned u, g;
        if (sscanf(as, "%u:%u", &u, &g) != 2) die("--as wants UID:GID");
        if (u == 0) die("will not write into a home as root (--as 0)");
        if (geteuid() == 0) {
            gid_t gg = g;
            if (setgroups(1, &gg) || setresgid(g, g, g) || setresuid(u, u, u))
                die("could not become %u:%u: %s", u, g, strerror(errno));
            if (u != 0 && setuid(0) == 0) die("still root after dropping privileges");
        } else if (getuid() != u || getgid() != g) {
            die("cannot become %u:%u without being root", u, g);
        }
    } else if (geteuid() == 0) {
        die("will not write into a home as root (pass --as UID:GID)");
    }
    umask(022);
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) { rl.rlim_cur = rl.rlim_max; setrlimit(RLIMIT_NOFILE, &rl); }
    if (!(buf = malloc(BUFSZ))) die("out of memory");

    char rel[PATH_MAX];
    t_start = t_last = last_commit = now_ns();
    for (int i = 0; i < njobs; i++) {               /* plan: totals first */
        rel[0] = '\0';
        if (jobs[i].srcfd < 0) {
            /* A link where a folder should be, or a folder we cannot
             * open: this one is reported, the others still come. */
            note("blocked", &jobs[i], "", "This folder could not be read from the Windows drive.");
            missing_any = 1;
            continue;
        }
        walk(&jobs[i], jobs[i].srcfd, -1, rel, 0, 0, 0);
    }
    t_start = now_ns();
    if (to_win && !dry_run && need_bytes > 0) {
        /* Is there room in Windows for all of it? Asked before a byte
         * moves, so a full C: never leaves half her photos there while
         * the rest is about to be removed with AurOS. */
        char probe[PATH_MAX];
        struct statvfs sv;
        long long free_b = -1, want = need_bytes + need_bytes / 50 + (64ll << 20);
        const char *ov = getenv("FERRY_ROOM_BYTES");          /* tests */
        if (ov && *ov) free_b = atoll(ov);
        else {
            snprintf(probe, sizeof probe, "%s", jobs[0].dst);
            for (;;) {
                if (statvfs(probe, &sv) == 0) { free_b = (long long)sv.f_bavail * (long long)sv.f_frsize; break; }
                char *sl = strrchr(probe, '/');
                if (!sl || sl == probe) break;
                *sl = '\0';
            }
        }
        if (free_b >= 0 && free_b < want) {
            char a[32], b[32];
            snprintf(a, sizeof a, "%lld", (want + (1 << 20) - 1) >> 20);
            snprintf(b, sizeof b, "%lld", free_b >> 20);
            if (fd_report >= 0)
                dprintf(fd_report, "%s\tblocked\tRoom\tThere is not enough room in Windows for your files: "
                        "they need about %s MB and Windows has %s MB free. Free some space in Windows, "
                        "then try again.\n", stage, a, b);
            if (fd_plain >= 0)
                dprintf(fd_plain, "There is not enough room in Windows for your files.\n");
            fprintf(stderr, "  -> not enough room: need %s MB, have %s MB\n", a, b);
            return 4;
        }
    }
    for (int i = 0; i < njobs && !out_of_room; i++) {
        struct job *j = &jobs[i];
        int dfd = -1;
        if (j->srcfd < 0) continue;
        cur_label = j->label;
        if (!dry_run) {
            if (mkdir_p(j->dst) != 0 ||
                (dfd = open(j->dst, O_RDONLY | O_DIRECTORY | O_CLOEXEC)) < 0) {
                note("blocked", j, "", "The folder for these could not be made on this computer.");
                missing_any = 1;
                continue;
            }
            clean_temps(dfd);
        }
        rel[0] = '\0';
        walk(j, j->srcfd, dfd, rel, 0, 0, 1);
        commit();
        if (dfd >= 0) close(dfd);
    }
    commit();
    if (out_of_room) {
        missing_any = 1;
        if (fd_report >= 0)
            dprintf(fd_report, "%s\tblocked\tRoom\tThere was not enough room on this "
                    "computer for everything; what fitted came across.\n", stage);
        if (fd_plain >= 0)
            dprintf(fd_plain, "There is not enough room on this computer for everything.\n");
    }
    tick(1);

    long long came = 0;
    for (int i = 0; i < njobs; i++) {
        struct job *j = &jobs[i];
        char msg[256];
        came += j->copied + j->already;
        snprintf(msg, sizeof msg, "%lld copied, %lld already here, %lld kept online by "
                 "OneDrive, %lld locked by Windows, %lld could not be read, %lld could not be saved",
                 j->copied, j->already, j->online, j->locked, j->unreadable, j->failed);
        note("info", j, "", msg);
        fprintf(stderr, "  -> %s: %s\n", j->label, msg);
    }
    /* There were files to bring and not one came: that is not "some
     * things could not be brought", it is "nothing could". */
    if (disp_total > 0 && came == 0 && !dry_run) return 1;
    return missing_any ? 2 : 0;
}
