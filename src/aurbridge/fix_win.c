/* fix_win.c — see fix.h. Windows only (the build puts *_win.c in both .exe
 * files and never in the simulation). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "fix.h"
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
      L"Drive C: is encrypted",
      L"AurOS can\u2019t go beside an encrypted drive yet.", NULL },
    { "bitlocker-other", FIX_CANNOT,
      L"Another drive in this computer is encrypted",
      L"AurOS can\u2019t go beside an encrypted drive yet.", NULL },
    { "pending-reboot", FIX_RESTART,
      L"Windows is waiting to restart to finish an update",
      L"We restart it. This installer opens again by itself afterwards.", NULL },
    { "volume-dirty", FIX_RESTART,
      L"Windows wants to check drive C: for errors",
      L"We restart so Windows can check it. This installer opens again after.", NULL },
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
      L"We restart Windows and check again. This installer opens again by itself.", NULL },
    { "fde-unreadable", FIX_RESTART,
      L"We couldn’t check whether drive C: is encrypted",
      L"We restart Windows and check again. This installer opens again by itself.", NULL },
    { "fde-not-checked", FIX_RESTART,
      L"We couldn’t check whether drive C: is encrypted",
      L"We restart Windows and check again. This installer opens again by itself.", NULL },
    { "fde-check-failed", FIX_RESTART,
      L"We couldn’t check this PC for encryption programs",
      L"We restart Windows and check again. This installer opens again by itself.", NULL },
    { "space-unmeasurable", FIX_RESTART,
      L"We couldn’t measure the free space on drive C:",
      L"We restart Windows and check again. This installer opens again by itself.", NULL },
    { "not-elevated", FIX_CANNOT,
      L"The installer wasn’t given permission to make changes",
      L"Close it, open it again, and choose Yes when Windows asks.", NULL },
    { "arm-pc", FIX_CANNOT,
      L"AurOS doesn’t run on this kind of computer yet",
      L"This computer has a newer kind of chip (ARM) that AurOS isn’t made for.", NULL },
    { "firmware-bios", FIX_CANNOT,
      L"This computer starts up the older way (BIOS)",
      L"AurOS can’t go on it yet. The AurOS website says what can be done.", NULL },
    { "intel-rst", FIX_CANNOT,
      L"AurOS can’t see this computer’s drive",
      L"The drive is set up in a way AurOS can’t use yet. Nothing was changed.", NULL },
    { "mbr-four-primaries", FIX_CANNOT,
      L"This drive has no room for another section",
      L"AurOS can’t install on this drive’s layout yet.", NULL },
    { "smart-bad-sectors", FIX_CANNOT,
      L"This drive is wearing out",
      L"Installing could lose your files. The drive needs replacing first.", NULL },
    { "smart-predict-failure", FIX_CANNOT,
      L"This drive says it is about to fail",
      L"Installing could lose your files. The drive needs replacing first.", NULL },
    { "dynamic-disk", FIX_CANNOT,
      L"Windows is on a multi-drive setup",
      L"AurOS can’t install on this kind of setup.", NULL },
    { "storage-spaces", FIX_CANNOT,
      L"Windows is on a Storage Spaces pool",
      L"AurOS can’t install on this kind of setup.", NULL },
    { "spanned-system-volume", FIX_CANNOT,
      L"Drive C: is spread over more than one disk",
      L"AurOS can’t install on this kind of setup.", NULL },
    { "fde-third-party", FIX_CANNOT,
      L"Another encryption program protects this drive",
      L"Switch it off in that program, then open this installer again.", NULL },
    { "fde-managed", FIX_CANNOT,
      L"This PC’s encryption is managed by an organisation",
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
      L"We couldn’t identify one of this PC’s drives",
      L"AurOS won’t write anywhere it can’t be sure of.", NULL },
    { "no-disks", FIX_CANNOT,
      L"We couldn’t read any drive in this PC",
      L"AurOS won’t write to a drive it can’t read.", NULL },
    { "sector-size-unknown", FIX_CANNOT,
      L"We couldn’t read how the Windows drive stores data",
      L"AurOS won’t write to a drive it can’t read.", NULL },
    { "system-disk-removable", FIX_CANNOT,
      L"Windows is running from a USB drive",
      L"AurOS installs only onto the PC’s own drive.", NULL },
    { "system-not-ntfs", FIX_CANNOT,
      L"Drive C: isn’t formatted the usual way",
      L"AurOS can only make room on an NTFS drive.", NULL },
    { "esp-missing", FIX_CANNOT,
      L"This PC’s start-up area isn’t on the Windows drive",
      L"AurOS can’t install on this layout yet.", NULL },
    { "esp-unreadable", FIX_CANNOT,
      L"We couldn’t read this PC’s start-up area",
      L"AurOS won’t write where it can’t read.", NULL },
    { "esp-low-space", FIX_CANNOT,
      L"This PC’s start-up area is too full",
      L"AurOS can’t fit its start-up files on this PC yet.", NULL },
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
      L"switch it to the newer way; how is on " ADVANCED },
    { "intel-rst",
      L"Keep using Windows for now. Someone who knows computers can change one "
      L"setting so AurOS can see the drive; how is on " ADVANCED },
    { "smart-bad-sectors",
      L"Copy your photos and documents somewhere else today, while the drive "
      L"still works. Then have the drive replaced." },
    { "smart-predict-failure",
      L"Copy your photos and documents somewhere else today, while the drive "
      L"still works. Then have the drive replaced." },
    { "bitlocker-system",
      L"Keep using Windows for now, and leave its drive lock as it is: AurOS "
      L"will never ask you to switch it off." },
    { "bitlocker-other",
      L"Keep using Windows for now, and leave its drive lock as it is: AurOS "
      L"will never ask you to switch it off." },
    { "not-elevated",
      L"Close this, open AurOS Setup again from your Downloads, and press Yes "
      L"when Windows asks whether it may make changes." },
};

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

static uint64_t temp_folders(int del)
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER t;
    t.LowPart = now.dwLowDateTime; t.HighPart = now.dwHighDateTime;
    ULONGLONG cutoff = t.QuadPart - (ULONGLONG)TEMP_AGE_DAYS * 24 * 3600 * 10000000ULL;
    uint64_t bytes = 0;
    wchar_t d[MAX_PATH];
    DWORD k = GetTempPathW(MAX_PATH, d);
    if (k > 3 && k < MAX_PATH) {
        if (d[k - 1] == L'\\') d[k - 1] = 0;
        bytes += temp_walk(d, 0, cutoff, del);
    }
    if (GetWindowsDirectoryW(d, MAX_PATH)) {
        wcsncat(d, L"\\Temp", MAX_PATH - wcslen(d) - 1);
        bytes += temp_walk(d, 0, cutoff, del);
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

/* A scheduled task, not a Run key: Windows does not start a program that
 * needs administrator rights from a Run or RunOnce key, and this one does.
 * At this person's next sign-in, with their highest rights, once: the
 * installer removes the task as the first thing it does (fix_resume_done). */
int fix_resume_arm(char *why, size_t n)
{
    wchar_t self[MAX_PATH];
    char selfa[MAX_PATH * 3], user[256] = "", dom[256] = "", cmd[2048], tail[256];
    if (!GetModuleFileNameW(NULL, self, MAX_PATH) ||
        !WideCharToMultiByte(CP_UTF8, 0, self, -1, selfa, sizeof selfa, NULL, NULL)) {
        snprintf(why, n, "the installer could not find itself on this PC");
        return -1;
    }
    GetEnvironmentVariableA("USERNAME", user, sizeof user);
    GetEnvironmentVariableA("USERDOMAIN", dom, sizeof dom);
    snprintf(cmd, sizeof cmd,
             "schtasks.exe /create /f /tn \"" RESUME_TASK "\" /sc onlogon /it /rl highest "
             "/ru \"%s\\%s\" /tr \"\\\"%s\\\" --resume\"", dom, user, selfa);
    if (plat_run(cmd, tail, sizeof tail) != 0) {
        snprintf(why, n, "Windows would not let the installer open itself again after "
                         "the restart");
        return -1;
    }
    return 0;
}

void fix_resume_done(void)
{
    char tail[128];
    plat_run("schtasks.exe /delete /f /tn \"" RESUME_TASK "\"", tail, sizeof tail);
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
