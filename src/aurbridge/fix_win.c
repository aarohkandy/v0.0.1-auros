/* fix_win.c — see fix.h. Windows only (the build puts *_win.c in both .exe
 * files and never in the simulation). */
#define WIN32_LEAN_AND_MEAN
#define PSAPI_VERSION 2        /* GetMappedFileNameW from kernel32, no psapi.dll */
#include <windows.h>
#include <psapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>

#include "fix.h"
#include "format.h"            /* fmt_sha: the copy is checked against the original */
#include "plat.h"
#include "aurbridge-baked.h"   /* AUROS_SITE_W: the website, from build/aurbridge */

/* ── the table ─────────────────────────────────────────────────────── */

static const fix_info FIXES[] = {
    /* The fix line the page shows names the size it frees (wizard.c,
     * space_fix_line); this one is for a check that has not measured. */
    { "insufficient-space", FIX_AUTO,
      L"Not enough free space on drive C:",
      L"We delete Windows’ old temporary files. Nothing of yours.",
      L"We deleted what we safely could, and it still needs more room. Move "
      L"big files (videos, games) off C: and we carry on by ourselves." },
    /* D11: BitLocker is PAUSED for one restart, never decrypted. Until
     * the engine can pause it (AB_CHOICE_BITLOCKER_PAUSE, which makes
     * these a note instead of a stop), an encrypted drive is a plain
     * stop. This used to switch encryption off on every fixed drive. */
    { "bitlocker-system", FIX_CANNOT,
      L"Windows has locked this computer\u2019s drive",
      L"AurOS can\u2019t go beside a locked drive yet. Nothing was changed.", NULL },
    { "bitlocker-other", FIX_CANNOT,
      L"Windows has locked another drive in this computer",
      L"AurOS can\u2019t go beside a locked drive yet. Nothing was changed.", NULL },
    { "pending-reboot", FIX_RESTART,
      L"Windows is waiting to restart to finish an update",
      L"We restart Windows. AurOS Setup opens again by itself afterwards.", NULL },
    { "volume-dirty", FIX_RESTART,
      L"Windows wants to check drive C: for errors",
      L"We restart so Windows can check it. AurOS Setup opens again after.", NULL },
    { "not-on-ac", FIX_WAIT,
      L"The charger isn’t plugged in",
      L"Plug it in. We carry on by ourselves.", NULL },
    /* NOT STOPS ANY MORE (D4): a low battery on the charger, a USB stick
     * or memory card, an empty card reader. They are warnings now, said
     * in NOTES below, or nothing at all. */
    { "recovery-stick-ambiguous", FIX_WAIT,
      L"Two USB drives look the same to this computer",
      L"Take one of them out. We carry on by ourselves.", NULL },
    { "power-state-unknown", FIX_WAIT,
      L"We can’t tell if the charger is plugged in",
      L"Plug it in. We carry on by ourselves.", NULL },
    /* A restart is what usually clears these; once, not in a loop (the
     * wizard stops offering it after the installer has reopened). */
    { "bitlocker-unknown", FIX_RESTART,
      L"We couldn’t check whether drive C: is encrypted",
      L"We restart Windows and check again. AurOS Setup opens again by itself.", NULL },
    { "fde-unreadable", FIX_RESTART,
      L"We couldn’t check whether drive C: is encrypted",
      L"We restart Windows and check again. AurOS Setup opens again by itself.", NULL },
    { "fde-not-checked", FIX_RESTART,
      L"We couldn’t check whether drive C: is encrypted",
      L"We restart Windows and check again. AurOS Setup opens again by itself.", NULL },
    { "fde-check-failed", FIX_RESTART,
      L"We couldn’t check this computer for drive locks",
      L"We restart Windows and check again. AurOS Setup opens again by itself.", NULL },
    { "space-unmeasurable", FIX_RESTART,
      L"We couldn’t measure the free space on drive C:",
      L"We restart Windows and check again. AurOS Setup opens again by itself.", NULL },
    { "not-elevated", FIX_CANNOT,
      L"AurOS Setup wasn’t given permission to make changes",
      L"Close it, open it again, and choose Yes when Windows asks.", NULL },
    { "arm-pc", FIX_CANNOT,
      L"AurOS doesn’t run on this kind of computer yet",
      L"This computer has a kind of chip that AurOS isn’t made for yet.", NULL },
    { "firmware-bios", FIX_CANNOT,
      L"This computer starts up an older way",
      L"AurOS can’t go on it yet. A helper may be able to change that.", NULL },
    { "intel-rst", FIX_CANNOT,
      L"AurOS can’t see this computer’s drive",
      L"Its drive is set up in a way AurOS can’t use yet. A helper can change it.", NULL },
    { "raid-volume", FIX_CANNOT,
      L"This computer\u2019s drive is made of several drives",
      L"AurOS can\u2019t see a drive set up that way yet. Nothing was changed.", NULL },
    { "storage-controller-unknown", FIX_CANNOT,
      L"AurOS could not tell how this computer\u2019s drive is connected",
      L"AurOS won\u2019t go on without being sure it can see the drive.", NULL },
    { "mbr-four-primaries", FIX_CANNOT,
      L"This drive can’t be divided any further",
      L"AurOS can’t install on this drive’s layout yet.", NULL },
    { "smart-bad-sectors", FIX_CANNOT,
      L"This drive is wearing out",
      L"Installing could lose your files. The drive needs replacing first.", NULL },
    { "smart-predict-failure", FIX_CANNOT,
      L"This drive says it is about to fail",
      L"Installing could lose your files. The drive needs replacing first.", NULL },
    { "dynamic-disk", FIX_CANNOT,
      L"Windows is spread over several drives",
      L"AurOS can’t install on this kind of setup.", NULL },
    { "storage-spaces", FIX_CANNOT,
      L"Windows is spread over several drives",
      L"AurOS can’t install on this kind of setup.", NULL },
    { "spanned-system-volume", FIX_CANNOT,
      L"Drive C: is spread over more than one drive",
      L"AurOS can’t install on this kind of setup.", NULL },
    { "fde-third-party", FIX_CANNOT,
      L"Another program locks this drive",
      L"AurOS can’t go beside it. Nothing was changed.", NULL },
    { "fde-managed", FIX_CANNOT,
      L"This computer’s drive lock is managed by an organization",
      L"Ask whoever manages it; AurOS can’t change it.", NULL },
    { "system-disk-unknown", FIX_CANNOT,
      L"We couldn’t tell which drive Windows is on",
      L"AurOS won’t guess which drive to write to, so it stops here.", NULL },
    { "system-disk-unreadable", FIX_CANNOT,
      L"We couldn’t read the drive Windows is on",
      L"AurOS won’t write to a drive it can’t read.", NULL },
    { "layout-unreadable", FIX_CANNOT,
      L"We couldn’t read how the Windows drive is laid out",
      L"AurOS won’t write to a drive it can’t read.", NULL },
    { "disk-unidentified", FIX_CANNOT,
      L"We couldn’t identify one of this computer’s drives",
      L"AurOS won’t write anywhere it can’t be sure of.", NULL },
    { "no-disks", FIX_CANNOT,
      L"We couldn’t read any drive in this computer",
      L"AurOS won’t write to a drive it can’t read.", NULL },
    { "sector-size-unknown", FIX_CANNOT,
      L"We couldn’t read how the Windows drive stores data",
      L"AurOS won’t write to a drive it can’t read.", NULL },
    { "system-disk-removable", FIX_CANNOT,
      L"Windows is running from a USB drive",
      L"AurOS installs only onto the computer’s own drive.", NULL },
    { "system-not-ntfs", FIX_CANNOT,
      L"Drive C: isn’t formatted the usual way",
      L"AurOS can only make room on a drive set up the usual Windows way.", NULL },
    { "esp-missing", FIX_CANNOT,
      L"This computer starts Windows from another drive",
      L"AurOS can’t install on this layout yet.", NULL },
    { "esp-unreadable", FIX_CANNOT,
      L"We couldn’t read how this computer starts up",
      L"AurOS won’t write where it can’t read.", NULL },
    { "esp-low-space", FIX_CANNOT,
      L"There is no room for AurOS’s start-up files",
      L"AurOS can’t fit beside Windows’ start-up files here yet.", NULL },
    /* From the hardware check (stream HW, hwinv_win.h) through the wizard,
     * not preflight.c's add(): a Wi-Fi card AurOS cannot drive and no
     * cable socket. She can fix it herself with a USB stick, and the page
     * checks again by itself, so it waits (the page shows HW's own
     * sentence, which says which stick and why). */
    { "no-internet", FIX_WAIT,
      L"AurOS would have no internet on this computer",
      L"Plug in a USB Wi-Fi stick. Show a helper how says which. We carry on by ourselves.", NULL },
    /* From sbdb.c, not preflight.c's add(): the wizard gives the first its
     * own page with the sketch (DESIGN-DIRECTION.md B-5). */
    { "secure-boot-ca", FIX_CANNOT,
      L"This computer is set to start only Windows",
      L"Someone who knows computers can change one setting in about five minutes.", NULL },
    { "secure-boot-ca-newer", FIX_CANNOT,
      L"This computer needs a newer AurOS Setup",
      L"Nothing here needs changing. Get AurOS Setup again later.", NULL },
};

/* ── warnings: one line each ─────────────────────────────────────── */

static const struct { const char *id; const wchar_t *line; } NOTES[] = {
    { "removable-attached",
      L"A USB stick or memory card is plugged in. That\u2019s fine: AurOS won\u2019t touch it." },
    { "removable-no-serial",
      L"A USB stick or memory card is plugged in. That\u2019s fine: AurOS won\u2019t touch it." },
    { "recovery-stick-ambiguous",
      L"A USB stick or memory card is plugged in. That\u2019s fine: AurOS won\u2019t touch it." },
    { "low-ram",
      L"This computer has little memory, so AurOS may be slow with many things open." },
    { "smart-reallocated",
      L"This computer\u2019s drive shows some wear. Keep copies of your photos somewhere else too." },
    { "dynamic-disk-other",
      L"Another drive in this computer has an unusual setup. AurOS won\u2019t touch it." },
    { "storage-spaces-other",
      L"Another drive in this computer has an unusual setup. AurOS won\u2019t touch it." },
    { "fde-container-software",
      L"Encryption software is installed. Close anything you have open in it first." },
    { "intel-rst",
      L"This computer\u2019s drive uses Intel\u2019s storage setting. AurOS checks it can see it." },
};

/* ── when it cannot go on: what she can do instead ────────────────── */

/* The page for "someone who knows computers" (website/advanced.html):
 * switching a BIOS-mode PC to the newer way, and Intel's "RAID On" to
 * AHCI, safely. Written out in full, because she will copy it down.
 * The site's address is baked by build/aurbridge from the repository it
 * builds for (tools/repo.sh), so a move to another repository edits
 * nothing here. */
#define ADVANCED AUROS_SITE_W L"advanced.html"

static const struct { const char *id; const wchar_t *line; } INSTEAD[] = {
    { "arm-pc",
      L"Keep using Windows on this computer. AurOS goes on most other Windows "
      L"computers, so a different one will probably be fine." },
    { "firmware-bios",
      L"Keep using Windows for now. Someone who knows computers may be able to "
      L"switch it to the newer way: press Show a helper how." },
    { "intel-rst",
      L"Keep using Windows for now. Someone who knows computers can change one "
      L"setting so AurOS can see the drive: press Show a helper how." },
    /* NOT the Intel-RST page: switching a RAID volume to AHCI breaks the
     * volume, and with it Windows and everything on it. */
    { "raid-volume",
      L"Keep using Windows. AurOS can\u2019t go on a drive set up this way, and "
      L"changing the setup would erase what is on it." },
    { "secure-boot-ca-newer",
      L"Keep using Windows for now. A newer AurOS Setup will go on this computer "
      L"without changing anything here." },
    { "smart-bad-sectors",
      L"Copy your photos and documents somewhere else today, while the drive "
      L"still works. Then have the drive replaced." },
    { "smart-predict-failure",
      L"Copy your photos and documents somewhere else today, while the drive "
      L"still works. Then have the drive replaced." },
    { "bitlocker-system",
      L"This is a Windows safety feature that comes on by default. It is not a "
      L"password you forgot. Keep using Windows for now, and leave it on: AurOS "
      L"will work on locked drives soon." },
    { "bitlocker-other",
      L"This is a Windows safety feature that comes on by default. It is not a "
      L"password you forgot. Keep using Windows for now, and leave it on: AurOS "
      L"will work on locked drives soon." },
    { "not-elevated",
      L"Close this, open AurOS Setup again from your Downloads, and press Yes "
      L"when Windows asks whether it may make changes." },
};

/* THE PAGE A HELPER READS, opened in her browser by "Show a helper how"
 * (no address is printed on her screen: DESIGN-DIRECTION.md B-8). */
const wchar_t *fix_helper_url(const char *id)
{
    if (!strcmp(id, "firmware-bios")) return ADVANCED L"#bios";
    if (!strcmp(id, "intel-rst"))     return ADVANCED L"#rst";
    if (!strcmp(id, "no-internet"))   return ADVANCED L"#wifi-stick";
    return NULL;
}

const wchar_t *fix_instead(const char *id)
{
    for (size_t i = 0; i < sizeof INSTEAD / sizeof INSTEAD[0]; i++)
        if (!strcmp(INSTEAD[i].id, id)) return INSTEAD[i].line;
    return NULL;
}

const wchar_t *fix_note(const char *id)
{
    for (size_t i = 0; i < sizeof NOTES / sizeof NOTES[0]; i++)
        if (!strcmp(NOTES[i].id, id)) return NOTES[i].line;
    return NULL;
}

const fix_info *fix_lookup(const char *id)
{
    for (size_t i = 0; i < sizeof FIXES / sizeof FIXES[0]; i++)
        if (!strcmp(FIXES[i].id, id)) return &FIXES[i];
    return NULL;
}

const fix_info *fix_at(int i)
{
    if (i < 0 || (size_t)i >= sizeof FIXES / sizeof FIXES[0]) return NULL;
    return &FIXES[i];
}

/* ── freeing space ─────────────────────────────────────────────────── */

/* ONLY WINDOWS' OWN TEMPORARY FILES, AND ONLY OLD ONES. It used to empty
 * her Recycle Bin (that is deleting her files) and run Disk Cleanup's
 * "Update Cleanup" and driver packages (the way back from an update);
 * both are gone (atlas review, finding 9). What is left is what Windows'
 * own Disk Cleanup calls "Temporary files": files in the two temp
 * folders that nothing has written for a week. And the page says how
 * much that is before she presses (fix_temp_bytes, the same walk).
 *
 * NEVER THROUGH A LINK, AND NEVER A LINK. A junction or symbolic link in
 * a temp folder can point anywhere -- at somebody's documents -- and a
 * recursive delete that follows it deletes them. Links are left as they
 * are, and so are folders: an empty folder frees nothing. A file in use
 * is skipped, not an error. */
#define TEMP_AGE_DAYS 7

static uint64_t temp_walk(const wchar_t *dir, int depth, ULONGLONG cutoff, int del)
{
    if (depth > 32) return 0;
    wchar_t pat[MAX_PATH * 2];
    _snwprintf(pat, MAX_PATH * 2 - 1, L"%ls\\*", dir);
    pat[MAX_PATH * 2 - 1] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    uint64_t bytes = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        DWORD a = fd.dwFileAttributes;
        if (a & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        wchar_t p[MAX_PATH * 2];
        _snwprintf(p, MAX_PATH * 2 - 1, L"%ls\\%ls", dir, fd.cFileName);
        p[MAX_PATH * 2 - 1] = 0;
        if (a & FILE_ATTRIBUTE_DIRECTORY) { bytes += temp_walk(p, depth + 1, cutoff, del); continue; }
        ULARGE_INTEGER w, c;
        w.LowPart = fd.ftLastWriteTime.dwLowDateTime;  w.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        c.LowPart = fd.ftCreationTime.dwLowDateTime;   c.HighPart = fd.ftCreationTime.dwHighDateTime;
        if (w.QuadPart >= cutoff || c.QuadPart >= cutoff) continue;   /* written this week */
        uint64_t n = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        if (!del) { bytes += n; continue; }
        if (a & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(p, a & ~FILE_ATTRIBUTE_READONLY);
        if (DeleteFileW(p)) bytes += n;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return bytes;
}

/* THE ONLY TWO FOLDERS EVER SWEPT, by their names and not by what the
 * environment says. This used to sweep whatever GetTempPathW returned:
 * TEMP and TMP are ordinary settings, a person (or a program) can point
 * them at her profile or at a folder she uses, and a sweep of "temporary
 * files" there deletes her files (atlas review, the safety skeptics).
 * Now: the Temp folder inside this user's own LocalAppData, and Temp
 * inside the Windows folder, exactly; and neither if it is a link. */
int fix_temp_roots(wchar_t out[2][MAX_PATH])
{
    int n = 0;
    wchar_t d[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, d) == S_OK &&
        wcslen(d) + 6 < MAX_PATH) {
        wcscat(d, L"\\Temp");
        wcscpy(out[n++], d);
    }
    if (GetWindowsDirectoryW(d, MAX_PATH) && wcslen(d) + 6 < MAX_PATH) {
        wcscat(d, L"\\Temp");
        wcscpy(out[n++], d);
    }
    return n;
}

static uint64_t temp_folders(int del)
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER t;
    t.LowPart = now.dwLowDateTime; t.HighPart = now.dwHighDateTime;
    ULONGLONG cutoff = t.QuadPart - (ULONGLONG)TEMP_AGE_DAYS * 24 * 3600 * 10000000ULL;
    uint64_t bytes = 0;
    wchar_t roots[2][MAX_PATH];
    int n = fix_temp_roots(roots);
    for (int i = 0; i < n; i++) {
        DWORD a = GetFileAttributesW(roots[i]);
        if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY) ||
            (a & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        bytes += temp_walk(roots[i], 0, cutoff, del);
    }
    return bytes;
}

uint64_t fix_temp_bytes(void) { return temp_folders(0); }

static int free_space(char *why, size_t n)
{
    (void)why; (void)n;
    temp_folders(1);
    return 0;
}

int fix_run(const char *id, char *why, size_t n)
{
    if (!strcmp(id, "insufficient-space")) return free_space(why, n);
    snprintf(why, n, "there is no fix for this that a program can do");
    return -1;
}

/* ── restart, and carry on ─────────────────────────────────────────── */

#define RESUME_TASK "AurOS installer - continue"

/* WHERE THE TASK STARTS IT FROM: a copy in Program Files, which only an
 * administrator can write. The task runs with her highest rights and no
 * question at sign-in, so it must never start a file that something
 * without those rights could have changed -- and her Downloads folder,
 * where she ran it from, is writable by every program she runs (atlas
 * review, finding 13). The folder is found through the registry
 * (SHGetFolderPath), not %ProgramFiles%, which she could set herself. */
static int resume_copy_path(wchar_t *dir, wchar_t *file, size_t n, const wchar_t *leaf)
{
    wchar_t pf[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL, SHGFP_TYPE_CURRENT, pf) != S_OK)
        return -1;
    if (_snwprintf(dir, n, L"%ls\\AurOS Setup", pf) < 0) return -1;
    if (_snwprintf(file, n, L"%ls\\%ls", dir, leaf) < 0) return -1;
    dir[n - 1] = 0; file[n - 1] = 0;
    return 0;
}

static int sha_file(HANDLE h, unsigned char out[32])
{
    static unsigned char buf[1 << 16];
    fmt_sha s;
    fmt_sha_start(&s);
    LARGE_INTEGER zero = { 0 };
    if (!SetFilePointerEx(h, zero, NULL, FILE_BEGIN)) return -1;
    DWORD got;
    while (ReadFile(h, buf, sizeof buf, &got, NULL) && got) fmt_sha_feed(&s, buf, got);
    fmt_sha_done(&s, out);
    return 0;
}

/* Copies this program, as it is running, into Program Files, and checks
 * the copy against it. The original is read through the file Windows
 * mapped this process from, not its name in Downloads: a running
 * program's file can be renamed and another put in its place. */
static int resume_copy(wchar_t *dst, size_t n, char *why, size_t wn)
{
    wchar_t self[MAX_PATH], dev[MAX_PATH], src[MAX_PATH + 32], dir[MAX_PATH];
    DWORD k = GetModuleFileNameW(NULL, self, MAX_PATH);
    if (!k || k >= MAX_PATH) { snprintf(why, wn, "AurOS Setup could not find itself on this computer"); return -1; }
    const wchar_t *leaf = wcsrchr(self, L'\\');
    leaf = leaf ? leaf + 1 : self;
    if (resume_copy_path(dir, dst, n, leaf) != 0) {
        snprintf(why, wn, "Windows would not say where its programs go");
        return -1;
    }
    if (!_wcsicmp(self, dst)) return 0;            /* already running from the copy */
    wcscpy(src, self);
    if (GetMappedFileNameW(GetCurrentProcess(), (LPVOID)GetModuleHandleW(NULL), dev, MAX_PATH))
        _snwprintf(src, MAX_PATH + 31, L"\\\\?\\GLOBALROOT%ls", dev);
    src[MAX_PATH + 31] = 0;
    /* Nobody may change the original while it is copied. */
    HANDLE in = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (in == INVALID_HANDLE_VALUE)
        in = CreateFileW(self, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (in == INVALID_HANDLE_VALUE) { snprintf(why, wn, "AurOS Setup could not read itself"); return -1; }
    CreateDirectoryW(dir, NULL);                   /* Program Files' own permissions */
    HANDLE out = CreateFileW(dst, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    int ok = out != INVALID_HANDLE_VALUE;
    static unsigned char buf[1 << 16];
    DWORD got, put;
    while (ok && ReadFile(in, buf, sizeof buf, &got, NULL) && got)
        ok = WriteFile(out, buf, got, &put, NULL) && put == got;
    unsigned char a[32], b[32];
    ok = ok && FlushFileBuffers(out) && sha_file(in, a) == 0 && sha_file(out, b) == 0 &&
         !memcmp(a, b, 32);
    CloseHandle(in);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!ok) {
        DeleteFileW(dst);
        snprintf(why, wn, "AurOS Setup could not put a copy of itself where Windows "
                          "keeps programs");
        return -1;
    }
    return 0;
}

/* A scheduled task, not a Run key: Windows does not start a program that
 * needs administrator rights from a Run or RunOnce key, and this one does.
 * At this person's next sign-in, with their highest rights, once: the
 * installer removes the task, and the copy it started, as the first thing
 * it does (fix_resume_done). */
int fix_resume_arm(char *why, size_t n)
{
    wchar_t copy[MAX_PATH];
    char copya[MAX_PATH * 3], user[256] = "", dom[256] = "", cmd[2048], tail[256];
    if (resume_copy(copy, MAX_PATH, why, n) != 0) return -1;
    if (!WideCharToMultiByte(CP_UTF8, 0, copy, -1, copya, sizeof copya, NULL, NULL)) {
        snprintf(why, n, "AurOS Setup could not find itself on this computer");
        return -1;
    }
    GetEnvironmentVariableA("USERNAME", user, sizeof user);
    GetEnvironmentVariableA("USERDOMAIN", dom, sizeof dom);
    snprintf(cmd, sizeof cmd,
             "schtasks.exe /create /f /tn \"" RESUME_TASK "\" /sc onlogon /it /rl highest "
             "/ru \"%s\\%s\" /tr \"\\\"%s\\\" --resume\"", dom, user, copya);
    if (plat_run(cmd, tail, sizeof tail) != 0) {
        fix_resume_done();             /* takes the copy away again */
        snprintf(why, n, "Windows would not let AurOS Setup open itself again after "
                         "the restart");
        return -1;
    }
    return 0;
}

void fix_resume_done(void)
{
    char tail[128];
    plat_run("schtasks.exe /delete /f /tn \"" RESUME_TASK "\"", tail, sizeof tail);
    /* And the copy: now, or, when this is it running, at the next restart. */
    wchar_t self[MAX_PATH], dir[MAX_PATH], copy[MAX_PATH];
    DWORD k = GetModuleFileNameW(NULL, self, MAX_PATH);
    if (!k || k >= MAX_PATH) return;
    const wchar_t *leaf = wcsrchr(self, L'\\');
    if (resume_copy_path(dir, copy, MAX_PATH, leaf ? leaf + 1 : self) != 0) return;
    if (GetFileAttributesW(copy) == INVALID_FILE_ATTRIBUTES) return;
    if (_wcsicmp(self, copy) && DeleteFileW(copy)) { RemoveDirectoryW(dir); return; }
    MoveFileExW(copy, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
    MoveFileExW(dir, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
}

int fix_restart_and_continue(char *why, size_t n)
{
    if (fix_resume_arm(why, n) != 0) return -1;
    if (plat_restart(why, n) != 0) {
        fix_resume_done();
        return -1;
    }
    return 0;
}
