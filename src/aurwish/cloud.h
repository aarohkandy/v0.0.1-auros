/* cloud.h — the engine's one conversation with the Wishes service
 * (docs/AI.md §5, docs/CLOUD.md). `aurwish` is the only client.
 *
 * WHY curl AND NOT A TLS LIBRARY. A TLS stack linked into a program that
 * parses hostile input is a lot of code in the wrong place; curl is already
 * in the image, already audited by everyone, and a separate process: if the
 * network misbehaves it is curl that dies. The token never appears on a
 * command line (`ps` shows every user's arguments): it goes to curl through
 * a pipe as a config file, and the request body through another.
 *
 * ONE REQUEST AT A TIME (§7.9), no automatic retries -- except the single
 * silent re-registration after a 401, which §5 asks for. Nothing here ever
 * blocks the main loop: the proof of work is done in slices, and the curl
 * child is read from a poll()ed pipe. */
#ifndef AURWISH_CLOUD_H
#define AURWISH_CLOUD_H

#include <stddef.h>

typedef struct cloud cloud;

typedef struct {
    char url[200];          /* https://host[:port], no path; "" = not available */
    char token_path[640];   /* ~/.local/share/auros/wishes-token.json, 0600 */
    int  allow_http;        /* tests only (AURWISH_TEST builds) */
    int  consent_version;
    unsigned sensors;       /* 1<<S_COMPASS ... : what this computer has */
} cloud_cfg;

enum { CLOUD_WISH, CLOUD_ASK };

typedef struct {
    long long id;
    int   kind;             /* CLOUD_WISH or CLOUD_ASK */
    int   http;             /* the status code, or 0 when there was none */
    const char *err;        /* "offline" | "busy" | "broken" when http == 0 */
    char *body;             /* the answer (NUL-terminated, owned by cloud) */
    size_t len;
    int   retry_after;      /* seconds, from the Retry-After header; 0 if none */
} cloud_result;

cloud *cloud_new(const cloud_cfg *cfg);
void   cloud_free(cloud *c);

int    cloud_available(const cloud *c);
int    cloud_busy(const cloud *c);

/* Starts one request. `text` must already be cleaned (cloud_clean_text);
 * `lang` is checked here. Returns 0, or -1 with *err set to the error code
 * the shell should be told ("busy", "refused", "offline"). */
int    cloud_start(cloud *c, int kind, long long id, const char *text, const char *lang,
                   const char *look, long long now, const char **err);
void   cloud_cancel(cloud *c);

/* -1, or the descriptor main() should poll for POLLIN. */
int    cloud_fd(const cloud *c);
/* 1 while there is work to do on the CPU (the proof of work): poll with a
 * short timeout. */
int    cloud_cpu(const cloud *c);
/* Advances the request. Returns 1 and fills *out when it has finished
 * (the body stays valid until the next cloud_start). */
int    cloud_step(cloud *c, long long now, cloud_result *out);

/* §7.12: tells the service to forget her, deletes the token, and makes the
 * next install_id a new one. Best effort; never blocks. */
void   cloud_forget(cloud *c);

/* §7.12: before sending, long digit runs and e-mail addresses become
 * placeholders. Returns the code points kept, or -1 when `in` is not
 * valid text. Writes at most n bytes. */
int    cloud_clean_text(const char *in, char *out, size_t n);
/* A BCP 47 tag we are willing to send; otherwise "en". */
const char *cloud_lang(const char *in, char *out, size_t n);

/* SHA-256, and the proof of work check, exposed for tests. */
void   sha256_of(const void *data, size_t len, unsigned char out[32]);
int    pow_ok(const unsigned char h[32], int bits);

#endif
