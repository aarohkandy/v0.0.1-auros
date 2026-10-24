/* stub.c -- the program she downloads: it starts nothing she has not been
 * asked about, imports nothing but kernel32, and runs the real installer
 * from a folder nobody else can write (SEC-003).
 *
 * THE HOLE. The installer asks Windows for administrator rights, so it runs
 * with them from the first instruction, and it sits in Downloads, a folder any
 * program she runs can write to. While Windows loads a program it takes DLLs
 * from the program's own folder before System32 (all but the few it knows by
 * name), and the real installer's imports pull in system DLLs whose own
 * imports (cfgmgr32, cryptbase, cryptsp) are taken the same way: a planted
 * copy ran as administrator before main() could do anything about it
 * (docs/security/FINDINGS.md SEC-003; windows.yml, "KNOWN OPEN" step, found on
 * windows-latest, runs 40043760099 and 40047918166).
 *
 * THE CURE. This stub is what AurOS-Setup.exe is. It has the same manifest as
 * the installer (requireAdministrator: one UAC prompt, the same file, name and
 * path as before), and it imports kernel32 only (build/aurbridge refuses
 * anything else). A program that imports kernel32 only has nothing else to
 * load before main (a probe on windows-latest took none of the planted DLLs).
 * Then, ELEVATED, so that Administrators can own what it makes:
 *
 *   1. SetDefaultDllDirectories(System32): from here a LoadLibrary is System32's.
 *   2. a fresh folder with a random name under Windows' configured ProgramData,
 *      created in one call with its security descriptor (CREATE_NEW: if the
 *      name exists, it is refused, never accepted): owner Administrators, SYSTEM
 *      and Administrators full control and nobody else, not inheriting anything.
 *      The folder's real path must be the one asked for (no link on the way)
 *      and its owner and ACL are read back and must be exactly that.
 *   3. the installer, carried in this file, is written into it (CREATE_NEW), and
 *      its SHA-256 is checked against the one baked in when this was built.
 *   4. the installer is started FROM that folder, with that folder as its
 *      current directory, given the arguments this program was given; this
 *      waits for it and then removes the folder.
 *
 * A restart (Restart now) ends this program before it can clean up. The next run
 * removes folders left behind, only those whose name is exactly ours AND whose
 * owner and ACL are exactly ours, and only the one file the installer is, never
 * anything else.
 *
 * Built with no C library (-nostdlib): everything here is kernel32 and ntdll
 * (NtQuerySecurityObject, already in every process). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#ifndef CHILD_SHA256
#error "the build bakes AurOS Setup's SHA-256 in: -DCHILD_SHA256=\"<64 hex>\""
#endif
#define RES_CHILD 300                       /* RCDATA: the installer (stub.rc) */
#define PREFIX    L"AurOS-Setup-"
#define CHILD_EXE L"setup.exe"

static wchar_t g_base[MAX_PATH + 16];   /* <system drive>\ProgramData */
static wchar_t g_dir[MAX_PATH + 64];    /* ...\AurOS-Setup-<16 hex> */
static wchar_t g_exe[MAX_PATH + 96];    /* ...\setup.exe */
static wchar_t g_cmd[32768];            /* CreateProcessW's documented command-line limit */
static int g_have_private;              /* only remove a directory this run created */

/* Test builds only (never shipped; build/aurbridge refuses them in a release):
 *   STUB_FIXED_NAME   the folder's name is this, so a test can plant something at it
 *   STUB_NO_ACL_CHECK  skip reading the ACL back (Wine does not keep one)
 * and the three that exist only to be red in CI (windows.yml, mut-* branches):
 *   STUB_MUTANT_HERE        run the installer from beside this program
 *   STUB_MUTANT_INHERIT     make the folder with the ACL its parent hands down
 *   STUB_MUTANT_ACCEPT      accept a folder that already exists, and links on the way */

/* ── no C library ─────────────────────────────────────────────────── */
void *memset(void *d, int c, size_t n) { volatile unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    for (; n; n--, p++, q++) if (*p != *q) return *p < *q ? -1 : 1;
    return 0;
}

/* ── SHA-256 ──────────────────────────────────────────────────────── */
typedef struct { uint32_t h[8]; uint64_t len; uint32_t n; unsigned char b[64]; } sha_t;
static const uint32_t K256[64] = {
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
 0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
 0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
 0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_block(sha_t *s, const unsigned char *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, h;
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4*i] << 24) | ((uint32_t)p[4*i+1] << 16) | ((uint32_t)p[4*i+2] << 8) | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void sha_start(sha_t *s)
{
    static const uint32_t iv[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    for (int i = 0; i < 8; i++) s->h[i] = iv[i];
    s->len = 0; s->n = 0;
}
static void sha_feed(sha_t *s, const void *data, size_t n)
{
    const unsigned char *p = data;
    s->len += n;
    while (n) {
        size_t k = 64 - s->n; if (k > n) k = n;
        memcpy(s->b + s->n, p, k); s->n += (uint32_t)k; p += k; n -= k;
        if (s->n == 64) { sha_block(s, s->b); s->n = 0; }
    }
}
static void sha_hex(sha_t *s, char out[65])
{
    static const char hx[] = "0123456789abcdef";
    uint64_t bits = s->len * 8;
    unsigned char pad = 0x80, z = 0;
    sha_feed(s, &pad, 1);
    while (s->n != 56) sha_feed(s, &z, 1);
    unsigned char l[8];
    for (int i = 0; i < 8; i++) l[i] = (unsigned char)(bits >> (56 - 8 * i));
    sha_feed(s, l, 8);
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++) {
            unsigned char v = (unsigned char)(s->h[i] >> (24 - 8 * j));
            out[i * 8 + j * 2] = hx[v >> 4]; out[i * 8 + j * 2 + 1] = hx[v & 15];
        }
    out[64] = 0;
}

/* ── small helpers ────────────────────────────────────────────────── */
static size_t wlen(const wchar_t *s) { size_t n = 0; while (s[n]) n++; return n; }
static int wcmp_i(const wchar_t *a, const wchar_t *b)
{
    for (;; a++, b++) {
        wchar_t x = *a, y = *b;
        if (x >= L'a' && x <= L'z') x -= 32;
        if (y >= L'a' && y <= L'z') y -= 32;
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
}
static wchar_t *wcat(wchar_t *d, size_t cap, const wchar_t *s)
{
    size_t n = wlen(d), m = wlen(s);
    if (n + m + 1 > cap) { d[0] = 0; return NULL; }
    memcpy(d + n, s, (m + 1) * sizeof(wchar_t));
    return d;
}

/* ── what is said when it cannot go on ───────────────────────────── */
static int g_quiet;                 /* "--quiet-failure": no box, for a test with nobody to press OK */
static void fail(const wchar_t *why)
{
    if (g_have_private) {
        if (g_exe[0]) DeleteFileW(g_exe);
        RemoveDirectoryW(g_dir);
        g_have_private = 0;
    }
    if (g_quiet) ExitProcess(3);
    /* user32 is not linked (kernel32 only); by now loads are System32's */
    HMODULE u = LoadLibraryExW(L"user32.dll", NULL, 0x800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */);
    typedef int (WINAPI *PFN_MB)(HWND, LPCWSTR, LPCWSTR, UINT);
    PFN_MB mb = u ? (PFN_MB)(void *)GetProcAddress(u, "MessageBoxW") : NULL;
    static wchar_t msg[600];
    msg[0] = 0;
    wcat(msg, 600, L"AurOS Setup could not start safely, so it did not start, and nothing on this computer has been changed.\n\n");
    wcat(msg, 600, why);
    wcat(msg, 600, L"\n\nDownload AurOS Setup again from the AurOS website, or ask someone to help you.");
    if (mb) mb(NULL, msg, L"AurOS Setup", 0x10 /* MB_ICONERROR */);
    ExitProcess(1);
}

/* ── the one security descriptor, built by hand (no advapi32) ───────
 * self-relative, owner and group Administrators, a protected DACL of two
 * allowed ACEs, SYSTEM and Administrators, FILE_ALL_ACCESS, inherited by what
 * is made inside. */
static const unsigned char SID_SYSTEM[12] = { 1, 1, 0,0,0,0,0,5, 0x12,0,0,0 };               /* S-1-5-18 */
static const unsigned char SID_ADMINS[16] = { 1, 2, 0,0,0,0,0,5, 0x20,0,0,0, 0x20,2,0,0 };   /* S-1-5-32-544 */
#define FILE_ALL 0x001F01FFu
static unsigned char g_sd[20 + 16 + 16 + 8 + (8 + 12) + (8 + 16)];
static void sd_build(void)
{
    unsigned char *p = g_sd;
    uint16_t ctl = 0x0004 | 0x1000 | 0x8000;        /* DACL present | DACL protected | self-relative */
    memset(g_sd, 0, sizeof g_sd);
    p[0] = 1;                                        /* revision */
    memcpy(p + 2, &ctl, 2);
    uint32_t off_owner = 20, off_group = 36, off_dacl = 52;
    memcpy(p + 4, &off_owner, 4); memcpy(p + 8, &off_group, 4); memcpy(p + 16, &off_dacl, 4);
    memcpy(p + off_owner, SID_ADMINS, 16);
    memcpy(p + off_group, SID_ADMINS, 16);
    unsigned char *a = p + off_dacl;
    uint16_t acl_size = 8 + (8 + 12) + (8 + 16), count = 2;
    a[0] = 2;                                        /* ACL revision */
    memcpy(a + 2, &acl_size, 2); memcpy(a + 4, &count, 2);
    unsigned char *e = a + 8;
    uint16_t sz1 = 8 + 12, sz2 = 8 + 16;
    uint32_t mask = FILE_ALL;
    e[0] = 0; e[1] = 3; memcpy(e + 2, &sz1, 2); memcpy(e + 4, &mask, 4); memcpy(e + 8, SID_SYSTEM, 12);
    e += sz1;
    e[0] = 0; e[1] = 3; memcpy(e + 2, &sz2, 2); memcpy(e + 4, &mask, 4); memcpy(e + 8, SID_ADMINS, 16);
}

/* is this exactly our owner and ACL? (a self-relative descriptor as the system gives it) */
static int sid_is(const unsigned char *s, const unsigned char *want, size_t n) { return memcmp(s, want, n) == 0; }
static int sd_is_ours(const unsigned char *sd, DWORD len)
{
    if (len < 24 || sd[0] != 1) return 0;
    uint16_t ctl; uint32_t oo, od;
    memcpy(&ctl, sd + 2, 2); memcpy(&oo, sd + 4, 4); memcpy(&od, sd + 16, 4);
    if (!(ctl & 0x0004) || !(ctl & 0x1000) || !(ctl & 0x8000)) return 0;   /* DACL present, protected */
    if (!oo || oo + 16 > len || !sid_is(sd + oo, SID_ADMINS, 16)) return 0;
    if (!od || od + 8 > len) return 0;
    const unsigned char *a = sd + od;
    uint16_t asz, cnt;
    memcpy(&asz, a + 2, 2); memcpy(&cnt, a + 4, 2);
    if (a[0] != 2 && a[0] != 4) return 0;
    if (cnt != 2 || asz != 8 + (8 + 12) + (8 + 16) || od + asz > len) return 0;
    int saw_sys = 0, saw_adm = 0;
    const unsigned char *e = a + 8;
    for (int i = 0; i < 2; i++) {
        uint16_t sz; uint32_t mask;
        if (e + 8 > a + asz) return 0;
        memcpy(&sz, e + 2, 2); memcpy(&mask, e + 4, 4);
        if ((sz != 8 + 12 && sz != 8 + 16) || e + sz > a + asz) return 0;
        if (e[0] != 0 || e[1] != 3 || mask != FILE_ALL) return 0;   /* allowed, OI|CI, exactly full */
        if (sz == 8 + 12 && sid_is(e + 8, SID_SYSTEM, 12)) saw_sys++;
        else if (sz == 8 + 16 && sid_is(e + 8, SID_ADMINS, 16)) saw_adm++;
        else return 0;
        e += sz;
    }
    return saw_sys == 1 && saw_adm == 1;
}

typedef LONG (NTAPI *PFN_NtQuerySecurityObject)(HANDLE, ULONG, PVOID, ULONG, PULONG);
static unsigned char g_sdbuf[1024];
/* read owner+DACL of the directory open as `h`; 1 if ours */
static int acl_of_handle_is_ours(HANDLE h)
{
#ifdef STUB_NO_ACL_CHECK
    (void)h; return 1;
#else
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    PFN_NtQuerySecurityObject q = nt ? (PFN_NtQuerySecurityObject)(void *)GetProcAddress(nt, "NtQuerySecurityObject") : NULL;
    ULONG need = 0;
    if (!q || q(h, 1 | 4 /* OWNER | DACL */, g_sdbuf, sizeof g_sdbuf, &need) < 0) return 0;
    return sd_is_ours(g_sdbuf, need);
#endif
}

/* ── paths ────────────────────────────────────────────────────────── */
static int is_dir_not_link(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}

/* the real path of an open directory, without \\?\ */
static int final_path(HANDLE h, wchar_t *out, DWORD n)
{
    DWORD k = GetFinalPathNameByHandleW(h, out, n, 0 /* VOLUME_NAME_DOS */);
    if (!k || k >= n) return 0;
    if (out[0] == L'\\' && out[1] == L'\\' && out[2] == L'?' && out[3] == L'\\') {
        for (DWORD i = 0; i + 4 <= k; i++) out[i] = out[i + 4];
    }
    return 1;
}

static int dir_is_trustworthy(const wchar_t *path, int check_acl)
{
    HANDLE h = CreateFileW(path, FILE_READ_ATTRIBUTES | READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    BY_HANDLE_FILE_INFORMATION fi;
    static wchar_t real[MAX_PATH + 96];
    int ok = GetFileInformationByHandle(h, &fi)
          && (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
          && final_path(h, real, MAX_PATH + 96) && wcmp_i(real, path) == 0;
    if (ok && check_acl) ok = acl_of_handle_is_ours(h);
    CloseHandle(h);
    return ok;
}

/* Ask Windows for the configured common-program-data folder. It may be moved
 * away from C:\ProgramData. shell32 and ole32 are loaded only after harden(). */
static int program_data_path(wchar_t *out, size_t cap)
{
    static const GUID folder = { 0x62AB5D82, 0xFDC1, 0x4DC3,
        { 0xA9, 0xDD, 0x07, 0x0D, 0x1D, 0x49, 0x5D, 0x97 } };
    typedef LONG (WINAPI *PFN_GET)(const GUID *, DWORD, HANDLE, wchar_t **);
    typedef void (WINAPI *PFN_FREE)(void *);
    HMODULE shell = LoadLibraryExW(L"shell32.dll", NULL, 0x800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */);
    HMODULE ole = LoadLibraryExW(L"ole32.dll", NULL, 0x800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */);
    PFN_GET get = shell ? (PFN_GET)(void *)GetProcAddress(shell, "SHGetKnownFolderPath") : NULL;
    PFN_FREE free_mem = ole ? (PFN_FREE)(void *)GetProcAddress(ole, "CoTaskMemFree") : NULL;
    wchar_t *p = NULL;
    int ok = get && free_mem && get(&folder, 0, NULL, &p) >= 0 && p
          && wlen(p) + 1 <= cap && wcat(out, cap, p);
    if (p && free_mem) free_mem(p);
    if (ole) FreeLibrary(ole);
    if (shell) FreeLibrary(shell);
    return ok;
}

/* The downloaded file can be started by any unelevated process, which also
 * chooses its environment. The elevated installer uses these values for disk
 * selection, private task files and temporary payloads, so replace them with
 * Windows' answers before the child inherits anything. */
static int secure_child_environment(void)
{
    static wchar_t win[MAX_PATH + 1], temp[MAX_PATH + 16], safe_search[4 * MAX_PATH];
    static wchar_t sam[512];
    UINT n = GetWindowsDirectoryW(win, MAX_PATH);
    if (n < 3 || n >= MAX_PATH || win[1] != L':') return 0;
    wchar_t drive[3] = { win[0], L':', 0 };
    temp[0] = 0;
    safe_search[0] = 0;
    if (!wcat(temp, MAX_PATH + 16, win) || !wcat(temp, MAX_PATH + 16, L"\\Temp")
            || !is_dir_not_link(temp)
            || !wcat(safe_search, 4 * MAX_PATH, win) || !wcat(safe_search, 4 * MAX_PATH, L"\\System32;")
            || !wcat(safe_search, 4 * MAX_PATH, win) || !wcat(safe_search, 4 * MAX_PATH, L";")
            || !wcat(safe_search, 4 * MAX_PATH, win) || !wcat(safe_search, 4 * MAX_PATH, L"\\System32\\Wbem;")
            || !wcat(safe_search, 4 * MAX_PATH, win) || !wcat(safe_search, 4 * MAX_PATH, L"\\System32\\WindowsPowerShell\\v1.0\\"))
        return 0;

    typedef BOOLEAN (WINAPI *PFN_NAME)(int, wchar_t *, ULONG *);
    HMODULE sec = LoadLibraryExW(L"secur32.dll", NULL, 0x800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */);
    PFN_NAME get_name = sec ? (PFN_NAME)(void *)GetProcAddress(sec, "GetUserNameExW") : NULL;
    ULONG sn = (ULONG)(sizeof sam / sizeof sam[0]);
    int named = get_name && get_name(2 /* NameSamCompatible: DOMAIN\\user */, sam, &sn);
    if (sec) FreeLibrary(sec);
    if (!named) return 0;
    wchar_t *slash = sam;
    while (*slash && *slash != L'\\') slash++;
    if (*slash != L'\\' || slash == sam || !slash[1]) return 0;
    *slash = 0;

    if (!SetEnvironmentVariableW(L"ProgramData", g_base)
            || !SetEnvironmentVariableW(L"SystemDrive", drive)
            || !SetEnvironmentVariableW(L"SystemRoot", win)
            || !SetEnvironmentVariableW(L"windir", win)
            || !SetEnvironmentVariableW(L"TEMP", temp)
            || !SetEnvironmentVariableW(L"TMP", temp)
            || !SetEnvironmentVariableW(L"Path" /* wordlint:ignore -- Windows' name */, safe_search)
            || !SetEnvironmentVariableW(L"USERNAME", slash + 1)
            || !SetEnvironmentVariableW(L"USERDOMAIN", sam))
        return 0;
    SetEnvironmentVariableW(L"AURBRIDGE_STAGING_KARGS", NULL);
    SetEnvironmentVariableW(L"AURBRIDGE_TEST_FVE", NULL);
    SetEnvironmentVariableW(L"AURBRIDGE_TEST_RST_SERVICE", NULL);
    SetEnvironmentVariableW(L"__COMPAT_LAYER", NULL);
    return 1;
}

static void hex16(wchar_t *o, uint64_t v)
{
    static const wchar_t hx[] = L"0123456789abcdef";
    for (int i = 0; i < 16; i++) o[i] = hx[(v >> (60 - 4 * i)) & 15];
    o[16] = 0;
}
static int random64(uint64_t *v)
{
    /* Loaded only after the DLL search is restricted to System32. A name that a
     * process beside the download can predict is not random for this purpose: it
     * could plant that directory before CreateDirectoryW's atomic create. */
    typedef LONG (WINAPI *PFN)(void *, unsigned char *, ULONG, ULONG);
    HMODULE b = LoadLibraryExW(L"bcrypt.dll", NULL, 0x800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */);
    PFN gen = b ? (PFN)(void *)GetProcAddress(b, "BCryptGenRandom") : NULL;
    LONG st = gen ? gen(NULL, (unsigned char *)v, sizeof *v, 2 /* BCRYPT_USE_SYSTEM_PREFERRED_RNG */) : -1;
    if (b) FreeLibrary(b);
    return st >= 0;
}

/* ── the leftovers of a run that was cut short ───────────────────── */
static int is_our_name(const wchar_t *n)
{
    size_t p = wlen(PREFIX);
    if (wlen(n) != p + 16) return 0;
    for (size_t i = 0; i < p; i++) if (n[i] != PREFIX[i]) return 0;
    for (size_t i = p; i < p + 16; i++) {
        wchar_t c = n[i];
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return 0;
    }
    return 1;
}
static void clean_stale(void)
{
    static wchar_t pat[MAX_PATH + 32], d[MAX_PATH + 96], f[MAX_PATH + 96];
    WIN32_FIND_DATAW fd;
    pat[0] = 0; wcat(pat, MAX_PATH + 32, g_base); wcat(pat, MAX_PATH + 32, L"\\" PREFIX L"*");
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        if (!is_our_name(fd.cFileName)) continue;
        d[0] = 0; wcat(d, MAX_PATH + 96, g_base); wcat(d, MAX_PATH + 96, L"\\"); wcat(d, MAX_PATH + 96, fd.cFileName);
        if (!dir_is_trustworthy(d, 1)) continue;                 /* our name AND our owner and ACL, or hands off */
        f[0] = 0; wcat(f, MAX_PATH + 96, d); wcat(f, MAX_PATH + 96, L"\\" CHILD_EXE);
        DeleteFileW(f);                                          /* the one file it holds; a file in use stays */
        RemoveDirectoryW(d);                                     /* only an empty folder goes */
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* ── the command line after the program's own name ───────────────── */
static const wchar_t *args_of(const wchar_t *cl)
{
    if (*cl == L'"') { cl++; while (*cl && *cl != L'"') cl++; if (*cl) cl++; }
    else while (*cl && *cl != L' ' && *cl != L'\t') cl++;
    while (*cl == L' ' || *cl == L'\t') cl++;
    return cl;
}

/* ── the stub ─────────────────────────────────────────────────────── */
static int harden(void)
{
    typedef BOOL (WINAPI *PFN)(DWORD);
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    PFN f = k ? (PFN)(void *)GetProcAddress(k, "SetDefaultDllDirectories") : NULL;
    return f && f(0x800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */);
}

__attribute__((force_align_arg_pointer))
void stub_start(void)
{
    /* There is no safe weaker mode. In particular SetDllDirectoryW("") still
     * leaves other search locations in play for DLLs loaded later. */
    if (!harden()) ExitProcess(1);
    sd_build();
    {   /* "--quiet-failure" is the one thing this program reads from its arguments: it only
         * decides whether a failure says so in a box; every argument goes on to the installer */
        const wchar_t *a = args_of(GetCommandLineW());
        static const wchar_t q[] = L"--quiet-failure";
        int i = 0;
        while (q[i] && a[i] == q[i]) i++;
        g_quiet = !q[i] && (!a[i] || a[i] == L' ');
    }

    /* The trusted base comes from Windows' Known Folder API, not an environment
     * variable a launcher can set and not an assumption that it lives on C:. */
    g_base[0] = 0;
    if (!program_data_path(g_base, MAX_PATH + 16)) fail(L"Windows would not say where its programs' data belongs.");
    if (!is_dir_not_link(g_base)) fail(L"The folder for programs' data is missing or is not a plain folder.");

    clean_stale();

    /* a fresh folder, in one call, with its descriptor */
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = g_sd;
    int made = 0;
    for (int tries = 0; tries < 16 && !made; tries++) {
        wchar_t hx[17];
#ifdef STUB_FIXED_NAME
        (void)hx; (void)random64;
        g_dir[0] = 0; wcat(g_dir, MAX_PATH + 64, g_base); wcat(g_dir, MAX_PATH + 64, L"\\" PREFIX STUB_FIXED_NAME);
#else
        uint64_t rnd;
        (void)tries;
        if (!random64(&rnd)) fail(L"Windows could not make a safe random folder name.");
        hex16(hx, rnd);
        g_dir[0] = 0; wcat(g_dir, MAX_PATH + 64, g_base); wcat(g_dir, MAX_PATH + 64, L"\\" PREFIX); wcat(g_dir, MAX_PATH + 64, hx);
#endif
#ifdef STUB_MUTANT_INHERIT
        made = CreateDirectoryW(g_dir, NULL);                    /* what the parent hands down */
#else
        made = CreateDirectoryW(g_dir, &sa);
#endif
        if (!made) {
            DWORD e = GetLastError();
#ifdef STUB_MUTANT_ACCEPT
            if (e == ERROR_ALREADY_EXISTS) { made = 1; break; }  /* MUTANT: take what is there */
#endif
            if (e != ERROR_ALREADY_EXISTS && e != ERROR_ACCESS_DENIED) fail(L"A private folder could not be made.");
#ifdef STUB_FIXED_NAME
            fail(L"The private folder's name was taken.");        /* a test plants something at the one name */
#endif
        }
    }
    if (!made) fail(L"A private folder could not be made.");
    g_have_private = 1;
#if !defined(STUB_MUTANT_ACCEPT) && !defined(STUB_MUTANT_INHERIT)
    if (!dir_is_trustworthy(g_dir, 1)) fail(L"The private folder is not the one that was asked for, or not private.");
#endif

    /* the installer, from this file's own resource */
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(RES_CHILD), (LPCWSTR)RT_RCDATA);
    HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
    DWORD n = r ? SizeofResource(NULL, r) : 0;
    const unsigned char *bytes = g ? (const unsigned char *)LockResource(g) : NULL;
    if (!bytes || n < 1024) fail(L"This copy of AurOS Setup is damaged.");

#ifdef STUB_MUTANT_HERE
    {   /* MUTANT: beside this program, in Downloads */
        DWORD m = GetModuleFileNameW(NULL, g_dir, MAX_PATH);
        while (m && g_dir[m - 1] != L'\\') m--;
        g_dir[m ? m - 1 : 0] = 0;
    }
#endif
    g_exe[0] = 0; wcat(g_exe, MAX_PATH + 96, g_dir); wcat(g_exe, MAX_PATH + 96, L"\\" CHILD_EXE);
    BY_HANDLE_FILE_INFORMATION original_info;
    memset(&original_info, 0, sizeof original_info);
    HANDLE f = CreateFileW(g_exe, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (f == INVALID_HANDLE_VALUE) fail(L"The installer could not be put in the private folder.");
    for (DWORD done = 0; done < n; ) {
        DWORD w = 0, chunk = n - done > (1u << 20) ? (1u << 20) : n - done;
        if (!WriteFile(f, bytes + done, chunk, &w, NULL) || !w) { CloseHandle(f); fail(L"The installer could not be written."); }
        done += w;
    }
    if (!FlushFileBuffers(f)) { CloseHandle(f); fail(L"The installer could not be written safely."); }

    /* Verify the same handle that was created: no close/reopen path race and no
     * reparse object can stand in for the ordinary file we just wrote. */
    {
        static unsigned char buf[1 << 16];
        static wchar_t real[MAX_PATH + 96];
        char got[65];
        sha_t s;
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        if (!GetFileInformationByHandle(f, &original_info) || (original_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                || !final_path(f, real, MAX_PATH + 96) || wcmp_i(real, g_exe) != 0
                || !SetFilePointerEx(f, zero, NULL, FILE_BEGIN)) {
            CloseHandle(f); fail(L"The installer file is not the ordinary file that was asked for.");
        }
        sha_start(&s);
        for (;;) {
            DWORD got_n = 0;
            if (!ReadFile(f, buf, sizeof buf, &got_n, NULL)) { CloseHandle(f); fail(L"The installer could not be read back."); }
            if (!got_n) break;
            sha_feed(&s, buf, got_n);
        }
        sha_hex(&s, got);
        const char *want = CHILD_SHA256;
        int same = 1;
        for (int i = 0; i < 64; i++) if (got[i] != want[i]) same = 0;
        if (!same) { CloseHandle(f); fail(L"The installer is not the one this file was built with."); }
    }
    CloseHandle(f);

    /* Reopen and pin the exact file identity until CreateProcess has opened its
     * image section. FILE_SHARE_READ denies rename/delete during that last use. */
    HANDLE pin = CreateFileW(g_exe, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    BY_HANDLE_FILE_INFORMATION pinned_info;
    static wchar_t pinned_path[MAX_PATH + 96];
    if (pin == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(pin, &pinned_info)
            || (pinned_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            || pinned_info.dwVolumeSerialNumber != original_info.dwVolumeSerialNumber
            || pinned_info.nFileIndexHigh != original_info.nFileIndexHigh
            || pinned_info.nFileIndexLow != original_info.nFileIndexLow
            || !final_path(pin, pinned_path, MAX_PATH + 96) || wcmp_i(pinned_path, g_exe) != 0) {
        if (pin != INVALID_HANDLE_VALUE) CloseHandle(pin);
        fail(L"The installer file changed before it could start.");
    }

    /* run it from there, given what we were given */
    if (!secure_child_environment()) fail(L"Windows could not make a safe environment for AurOS Setup.");
    g_cmd[0] = 0;
    if (!wcat(g_cmd, 32768, L"\"") || !wcat(g_cmd, 32768, g_exe)
            || !wcat(g_cmd, 32768, L"\" ") || !wcat(g_cmd, 32768, args_of(GetCommandLineW())))
        fail(L"The command given to AurOS Setup is too long for Windows.");
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessW(g_exe, g_cmd, NULL, NULL, FALSE, 0, NULL, g_dir, &si, &pi)) {
        CloseHandle(pin);
        fail(L"The installer would not start.");
    }
    CloseHandle(pin);
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);

    DeleteFileW(g_exe);
#ifndef STUB_MUTANT_HERE
    RemoveDirectoryW(g_dir);
#endif
    g_have_private = 0;
    ExitProcess(code);
}
