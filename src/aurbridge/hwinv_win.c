/* hwinv_win.c -- the Windows half of the hardware inventory, and the
 * `--report` entry point. See hwinv.h.
 *
 * STRICTLY READ-ONLY. It lists devices (SetupAPI), asks the spooler
 * which printers exist, reads the maker's name out of the registry and
 * the machine's own state out of the installer's preflight. It writes
 * exactly two files, both named AurOS-report.*, into one folder.
 *
 * DLLs that build/aurbridge does not link (winspool.drv, iphlpapi.dll)
 * are loaded when needed: the build refuses an .exe that imports a DLL
 * outside its allow-list, and a missing one must not stop a report. */

/* winsock2.h first: iptypes.h and the older winsock.h that windows.h
 * would otherwise pull in both define sockaddr. Nothing here links
 * ws2_32; only the structure layouts are wanted. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include "hwinv.h"
#include "hwinv_win.h"
#include "hwcompat.h"
#include "preflight.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <winioctl.h>
#include <winspool.h>
#include <iptypes.h>
#include <shlobj.h>

#define HWINV_TOOL "AurOS Setup (PC report, format 1)"

/* ── small readers ───────────────────────────────────────────────── */
static void utf8(const WCHAR *w, char *out, size_t n)
{
    out[0] = 0;
    if (!w || !n) return;
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL))
        out[n - 1] = 0;           /* truncated: still terminated */
    out[n - 1] = 0;
}

static int reg_str(HKEY root, const char *sub, const char *val, char *out, size_t n)
{
    HKEY k; DWORD type = 0, sz = (DWORD)n - 1;
    out[0] = 0;
    if (RegOpenKeyExA(root, sub, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return 0;
    LONG rc = RegQueryValueExA(k, val, NULL, &type, (BYTE *)out, &sz);
    RegCloseKey(k);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) { out[0] = 0; return 0; }
    out[sz < n ? sz : n - 1] = 0;
    /* a registry string is often NUL-padded or has trailing blanks */
    for (size_t i = strlen(out); i > 0 && (out[i - 1] == ' ' || out[i - 1] == '\r'); i--) out[i - 1] = 0;
    return out[0] != 0;
}

/* ── the adapters Windows is using ───────────────────────────────── */
/* GAA_FLAG_SKIP_UNICAST | ANYCAST | MULTICAST | DNS_SERVER | FRIENDLY_NAME: only the name, type and state */
#define GAA_SKIP 0x2F
typedef struct { char guid[64]; int if_type; int up; } adapter;
#define MAX_ADAPTERS 64
static adapter g_ad[MAX_ADAPTERS];
static int g_nad;

typedef ULONG (WINAPI *PFN_GetAdaptersAddresses)(ULONG, ULONG, PVOID, PIP_ADAPTER_ADDRESSES, PULONG);

static void read_adapters(void)
{
    g_nad = 0;
    HMODULE m = LoadLibraryA("iphlpapi.dll");
    if (!m) return;
    PFN_GetAdaptersAddresses fn = (PFN_GetAdaptersAddresses)(void *)GetProcAddress(m, "GetAdaptersAddresses");
    if (fn) {
        ULONG sz = 0;
        fn(AF_UNSPEC, GAA_SKIP, NULL, NULL, &sz);
        PIP_ADAPTER_ADDRESSES buf = sz ? malloc(sz) : NULL;
        if (buf && fn(AF_UNSPEC, GAA_SKIP, NULL, buf, &sz) == NO_ERROR) {
            for (PIP_ADAPTER_ADDRESSES a = buf; a && g_nad < MAX_ADAPTERS; a = a->Next) {
                adapter *x = &g_ad[g_nad++];
                snprintf(x->guid, sizeof x->guid, "%s", a->AdapterName ? a->AdapterName : "");
                x->if_type = (int)a->IfType;
                x->up = a->OperStatus == 1;           /* IfOperStatusUp */
            }
        }
        free(buf);
    }
    FreeLibrary(m);
}

static const adapter *adapter_for(HDEVINFO set, SP_DEVINFO_DATA *dev)
{
    HKEY k = SetupDiOpenDevRegKey(set, dev, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
    if (k == INVALID_HANDLE_VALUE) return NULL;
    char id[64] = ""; DWORD type = 0, sz = sizeof id - 1;
    LONG rc = RegQueryValueExA(k, "NetCfgInstanceId", NULL, &type, (BYTE *)id, &sz);
    RegCloseKey(k);
    if (rc != ERROR_SUCCESS) return NULL;
    id[sizeof id - 1] = 0;
    for (int i = 0; i < g_nad; i++) if (!_stricmp(g_ad[i].guid, id)) return &g_ad[i];
    return NULL;
}

/* ── devices ─────────────────────────────────────────────────────── */
/* A property of a device node as UTF-8; for a multi-string (the hardware
 * IDs) the FIRST entry. "" when it is not there. */
static void dev_prop(HDEVINFO set, SP_DEVINFO_DATA *dev, DWORD prop, char *out, size_t n)
{
    WCHAR w[512]; DWORD type = 0, need = 0;
    out[0] = 0;
    if (SetupDiGetDeviceRegistryPropertyW(set, dev, prop, &type, (BYTE *)w, sizeof w - sizeof(WCHAR), &need)) {
        w[(sizeof w / sizeof w[0]) - 1] = 0;
        utf8(w, out, n);
    }
}

/* A multi-string property as one string, its entries joined with ';'. */
static void dev_multi(HDEVINFO set, SP_DEVINFO_DATA *dev, DWORD prop, char *out, size_t n)
{
    WCHAR w[1024]; DWORD type = 0, need = 0;
    size_t j = 0;
    out[0] = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(set, dev, prop, &type, (BYTE *)w, sizeof w - 2 * sizeof(WCHAR), &need))
        return;
    w[(sizeof w / sizeof w[0]) - 1] = 0;
    for (const WCHAR *p = w; *p && j + 2 < n; p += wcslen(p) + 1) {
        char one[160];
        utf8(p, one, sizeof one);
        for (size_t k = 0; one[k] && j + 2 < n; k++) out[j++] = one[k];
        out[j++] = ';';
    }
    out[j] = 0;
}

static void collect_devices(hw_inv *inv)
{
    read_adapters();
    HDEVINFO set = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return;
    SP_DEVINFO_DATA dev; dev.cbSize = sizeof dev;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &dev); i++) {
        char hwid[96], compat[400], cls[24], name[96], mfg[48], svc[32];
        dev_prop(set, &dev, SPDRP_HARDWAREID, hwid, sizeof hwid);
        if (!hwid[0]) { inv->n_dropped++; continue; }
        dev_multi(set, &dev, SPDRP_COMPATIBLEIDS, compat, sizeof compat);
        /* the class code is in the hardware ids too on some buses */
        if (strlen(compat) + 2 < sizeof compat) { size_t l = strlen(compat); compat[l] = ';'; compat[l + 1] = 0; }
        dev_prop(set, &dev, SPDRP_CLASS, cls, sizeof cls);
        dev_prop(set, &dev, SPDRP_FRIENDLYNAME, name, sizeof name);
        if (!name[0]) dev_prop(set, &dev, SPDRP_DEVICEDESC, name, sizeof name);
        dev_prop(set, &dev, SPDRP_MFG, mfg, sizeof mfg);
        dev_prop(set, &dev, SPDRP_SERVICE, svc, sizeof svc);

        ULONG status = 0, prob = 0; int problem = 0;
        if (CM_Get_DevNode_Status(&status, &prob, dev.DevInst, 0) == CR_SUCCESS && (status & DN_HAS_PROBLEM))
            problem = (int)prob;

        const adapter *ad = NULL;
        if (!_stricmp(cls, "Net")) ad = adapter_for(set, &dev);
        int k = hwinv_add_dev(inv, cls, hwid, compat, name, mfg, svc, problem, ad ? ad->if_type : 0);
        if (k >= 0 && ad) inv->devs[k].in_use = ad->up;
    }
    SetupDiDestroyDeviceInfoList(set);
}

/* ── printers ────────────────────────────────────────────────────── */
typedef BOOL (WINAPI *PFN_EnumPrintersW)(DWORD, LPWSTR, DWORD, LPBYTE, DWORD, LPDWORD, LPDWORD);
typedef BOOL (WINAPI *PFN_GetDefaultPrinterW)(LPWSTR, LPDWORD);

static void collect_usb_printers(hw_inv *inv)
{
    /* The USB bus's own view: one USBPRINT device per printer, whose
     * parent is the USB device that has the vendor and product id. */
    hw_usbprn u[HW_MAX_PRINTERS];
    int n = 0;
    HDEVINFO set = SetupDiGetClassDevsW(NULL, L"USBPRINT", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return;
    SP_DEVINFO_DATA dev; dev.cbSize = sizeof dev;
    for (DWORD i = 0; n < HW_MAX_PRINTERS && SetupDiEnumDeviceInfo(set, i, &dev); i++) {
        char hwid[96]; hw_id id;
        dev_prop(set, &dev, SPDRP_HARDWAREID, hwid, sizeof hwid);
        if (!hwinv_parse_hwid(hwid, &id) || !id.ieee[0]) continue;
        memset(&u[n], 0, sizeof u[n]);
        snprintf(u[n].ieee, sizeof u[n].ieee, "%s", id.ieee);
        DEVINST up;
        if (CM_Get_Parent(&up, dev.DevInst, 0) == CR_SUCCESS) {
            WCHAR w[512]; ULONG len = sizeof w - sizeof(WCHAR);
            if (CM_Get_DevNode_Registry_PropertyW(up, CM_DRP_HARDWAREID, NULL, w, &len, 0) == CR_SUCCESS) {
                char ph[96]; hw_id pid;
                utf8(w, ph, sizeof ph);
                if (hwinv_parse_hwid(ph, &pid) && pid.has_ids) { u[n].vid = pid.vid; u[n].pid = pid.did; }
            }
        }
        n++;
    }
    SetupDiDestroyDeviceInfoList(set);
    hwinv_pair_usb_printers(inv, u, n);
}

static void collect_printers(hw_inv *inv)
{
    HMODULE m = LoadLibraryA("winspool.drv");
    if (m) {
        PFN_EnumPrintersW en = (PFN_EnumPrintersW)(void *)GetProcAddress(m, "EnumPrintersW");
        PFN_GetDefaultPrinterW gd = (PFN_GetDefaultPrinterW)(void *)GetProcAddress(m, "GetDefaultPrinterW");
        WCHAR defname[256] = L""; DWORD dn = 256;
        if (gd && !gd(defname, &dn)) defname[0] = 0;
        DWORD need = 0, cnt = 0;
        /* LOCAL only: a connection to a print server asks that server,
         * and an unreachable one can make this wait a minute. */
        if (en) {
            en(PRINTER_ENUM_LOCAL, NULL, 2, NULL, 0, &need, &cnt);
            BYTE *buf = need ? malloc(need) : NULL;
            if (buf && en(PRINTER_ENUM_LOCAL, NULL, 2, buf, need, &need, &cnt)) {
                PRINTER_INFO_2W *p = (PRINTER_INFO_2W *)buf;
                for (DWORD i = 0; i < cnt; i++) {
                    char nm[96], dr[96], po[64];
                    utf8(p[i].pPrinterName, nm, sizeof nm);
                    utf8(p[i].pDriverName, dr, sizeof dr);
                    utf8(p[i].pPortName, po, sizeof po);
                    int def = defname[0] && p[i].pPrinterName && !wcscmp(defname, p[i].pPrinterName);
                    hwinv_add_printer(inv, nm, dr, po, def);
                }
            }
            free(buf);
        }
        FreeLibrary(m);
    }
    collect_usb_printers(inv);
}

/* ── the machine ─────────────────────────────────────────────────── */
static void collect_machine(hw_inv *inv)
{
    const char *bios = "HARDWARE\\DESCRIPTION\\System\\BIOS";
    reg_str(HKEY_LOCAL_MACHINE, bios, "SystemManufacturer", inv->dmi_vendor, sizeof inv->dmi_vendor);
    reg_str(HKEY_LOCAL_MACHINE, bios, "SystemProductName", inv->dmi_model, sizeof inv->dmi_model);
    if (!reg_str(HKEY_LOCAL_MACHINE, bios, "SystemFamily", inv->dmi_family, sizeof inv->dmi_family))
        reg_str(HKEY_LOCAL_MACHINE, bios, "SystemVersion", inv->dmi_family, sizeof inv->dmi_family);
    reg_str(HKEY_LOCAL_MACHINE, bios, "BaseBoardProduct", inv->dmi_board, sizeof inv->dmi_board);
    reg_str(HKEY_LOCAL_MACHINE, bios, "BIOSVendor", inv->bios_vendor, sizeof inv->bios_vendor);
    reg_str(HKEY_LOCAL_MACHINE, bios, "BIOSVersion", inv->bios_version, sizeof inv->bios_version);
    reg_str(HKEY_LOCAL_MACHINE, bios, "BIOSReleaseDate", inv->bios_date, sizeof inv->bios_date);
    reg_str(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            "ProcessorNameString", inv->cpu, sizeof inv->cpu);
    /* the registry pads the name with blanks on the left on some Intel parts */
    char *c = inv->cpu; while (*c == ' ') c++;
    if (c != inv->cpu) memmove(inv->cpu, c, strlen(c) + 1);

    SYSTEM_INFO si; GetSystemInfo(&si);
    inv->cpu_threads = (int)si.dwNumberOfProcessors;
    MEMORYSTATUSEX ms = { .dwLength = sizeof ms };
    if (GlobalMemoryStatusEx(&ms)) inv->ram_bytes = ms.ullTotalPhys;

    const char *cv = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    char build[16] = "";
    reg_str(HKEY_LOCAL_MACHINE, cv, "ProductName", inv->os_name, sizeof inv->os_name);
    if (!reg_str(HKEY_LOCAL_MACHINE, cv, "DisplayVersion", inv->os_display, sizeof inv->os_display))
        reg_str(HKEY_LOCAL_MACHINE, cv, "ReleaseId", inv->os_display, sizeof inv->os_display);
    reg_str(HKEY_LOCAL_MACHINE, cv, "CurrentBuild", build, sizeof build);
    inv->os_build = atoi(build);
    /* Windows 11 still calls itself "Windows 10 ..." in this value */
    if (inv->os_build >= 22000 && !strncmp(inv->os_name, "Windows 10", 10))
        inv->os_name[9] = '1';
}

static void collect_from_preflight(hw_inv *inv, const pf_report *r)
{
    inv->is_uefi = r->is_uefi;
    inv->secure_boot = r->secure_boot;
    inv->has_battery = r->has_battery;
    inv->pf_blocks = r->n_block; inv->pf_warns = r->n_warn;
    inv->pf_go = pf_is_go(r) ? 1 : 0;
    for (int i = 0; i < r->n && inv->pf_n_find < 16; i++) {
        const pf_result *x = &r->results[i];
        if (!strcmp(x->id, "arm-pc")) inv->is_arm = 1;
        if (x->sev == PF_PASS) continue;
        char *id = inv->pf_find[inv->pf_n_find].id;
        char *sev = inv->pf_find[inv->pf_n_find].sev;
        char *title = inv->pf_find[inv->pf_n_find].title;
        snprintf(id, sizeof inv->pf_find[0].id, "%s", x->id);
        snprintf(sev, sizeof inv->pf_find[0].sev, "%s",
                 x->sev == PF_BLOCK ? "STOP" : x->sev == PF_WARN ? "WARN" : "INFO");
        snprintf(title, sizeof inv->pf_find[0].title, "%s", x->title);
        /* Not being an administrator is not a verdict on the PC: the
         * installer's check did not run. (The installer asks for it up
         * front; this is the console tool.) */
        if (!strcmp(x->id, "not-elevated")) {
            inv->pf_go = -1; inv->pf_blocks = 0;
            snprintf(sev, sizeof inv->pf_find[0].sev, "INFO");
            snprintf(title, sizeof inv->pf_find[0].title,
                     "The installer's drive check did not run: this was not started as administrator");
        }
        inv->pf_n_find++;
    }
    for (int i = 0; i < r->n_volumes; i++) {
        if (r->volumes[i].bitlocker == 1) inv->bitlocker_any = 1;
        else if (r->volumes[i].bitlocker == 0 && inv->bitlocker_any != 1) inv->bitlocker_any = 0;
    }
    if (r->system_volume >= 0 && r->system_volume < r->n_volumes) {
        const pf_volume *v = &r->volumes[r->system_volume];
        inv->bitlocker = v->bitlocker;
        inv->sys_free_bytes = v->free_bytes;
    }
    for (int i = 0; i < r->n_disks; i++) {
        const pf_disk *d = &r->disks[i];
        if (d->index != r->system_disk) continue;
        snprintf(inv->sys_disk_model, sizeof inv->sys_disk_model, "%s", d->model);
        inv->sys_disk_bytes = d->size_bytes;
        inv->sys_disk_bus = d->bus_type;
        inv->sys_disk_kind = d->seek_penalty == 1 ? 0 : d->seek_penalty == 0 ? 1 : -1;
    }
}

int hwinv_collect(hw_inv *inv)
{
    collect_machine(inv);
    collect_devices(inv);
    collect_printers(inv);
    return 0;
}

int hwinv_scan(hw_inv *inv, const pf_report *pf)
{
    hwinv_collect(inv);
    if (pf) collect_from_preflight(inv, pf);
    hwcompat_judge(inv);
    return 0;
}

int hwinv_blocks(const hw_inv *inv)
{
    for (int i = 0; i < inv->n_verdicts; i++) if (inv->verdicts[i].blocks) return 1;
    return 0;
}

/* ── --report ────────────────────────────────────────────────────── */
static int write_file(const WCHAR *path, const char *bytes, size_t n, int bom)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0; int ok = 1;
    static const BYTE b[3] = { 0xEF, 0xBB, 0xBF };
    if (bom) ok = WriteFile(h, b, 3, &w, NULL);
    if (ok && n) ok = WriteFile(h, bytes, (DWORD)n, &w, NULL) && w == n;
    ok = CloseHandle(h) && ok;
    return ok;
}

/* A folder we can write the two files into: the one asked for, else the
 * one the program is in, else the Desktop, else the temp folder. */
static int pick_dir(const char *want, WCHAR *out, size_t n)
{
    WCHAR cand[4][MAX_PATH];
    int nc = 0;
    if (want && want[0]) MultiByteToWideChar(CP_UTF8, 0, want, -1, cand[nc++], MAX_PATH);
    if (!want || !want[0]) {
        WCHAR me[MAX_PATH];
        DWORD l = GetModuleFileNameW(NULL, me, MAX_PATH);
        if (l && l < MAX_PATH) {
            WCHAR *s = wcsrchr(me, L'\\');
            if (s) { *s = 0; wcscpy(cand[nc++], me); }
        }
        if (SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, cand[nc]) == S_OK) nc++;
        if (GetTempPathW(MAX_PATH, cand[nc])) nc++;
    }
    if (want && want[0]) CreateDirectoryW(cand[0], NULL);  /* one level; fine if it exists */
    for (int i = 0; i < nc; i++) {
        WCHAR probe[MAX_PATH + 32];
        _snwprintf(probe, MAX_PATH + 31, L"%ls\\AurOS-report.txt", cand[i]);
        probe[MAX_PATH + 31] = 0;
        /* try the real file name: if it can be made, the folder will do */
        HANDLE h = CreateFileW(probe, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            _snwprintf(out, n - 1, L"%ls", cand[i]);
            out[n - 1] = 0;
            return 1;
        }
    }
    return 0;
}

int hwinv_report_main(const char *dir, int gui, const char *tool)
{
    (void)tool;
    hw_inv *inv = hwinv_new();
    pf_report *pr = calloc(1, sizeof *pr);        /* 70 KB: not on the stack */
    if (!inv || !pr) { free(inv); free(pr); return 1; }

    pf_run(pr);
    hwinv_scan(inv, pr);
    free(pr);

    SYSTEMTIME t; GetLocalTime(&t);
    char made[48];
    snprintf(made, sizeof made, "%04d-%02d-%02d %02d:%02d (this PC's clock)",
             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);

    char *tx = hwinv_text(inv, HWINV_TOOL, made);
    char *js = hwinv_json(inv, HWINV_TOOL, made);
    WCHAR where[MAX_PATH];
    int ok = tx && js && pick_dir(dir, where, MAX_PATH);
    if (ok) {
        WCHAR p1[MAX_PATH + 32], p2[MAX_PATH + 32];
        _snwprintf(p1, MAX_PATH + 31, L"%ls\\AurOS-report.txt", where);
        _snwprintf(p2, MAX_PATH + 31, L"%ls\\AurOS-report.json", where);
        p1[MAX_PATH + 31] = p2[MAX_PATH + 31] = 0;
        ok = write_file(p1, tx, strlen(tx), 1) && write_file(p2, js, strlen(js), 0);
        if (gui) {
            WCHAR msg[MAX_PATH + 200];
            _snwprintf(msg, MAX_PATH + 199,
                       ok ? L"This PC's report is saved here:\n\n%ls\n\n"
                            L"AurOS-report.txt is the one to read or send. Nothing on this PC was changed."
                          : L"The report could not be saved in\n\n%ls", where);
            msg[MAX_PATH + 199] = 0;
            MessageBoxW(NULL, msg, L"AurOS - PC report", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
        }
    } else if (gui) {
        MessageBoxW(NULL, L"The report could not be saved: no folder on this PC would take it.",
                    L"AurOS - PC report", MB_OK | MB_ICONWARNING);
    }
    free(tx); free(js);
    hwinv_free(inv);
    return ok ? 0 : 1;
}
