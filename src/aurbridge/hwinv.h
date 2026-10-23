/* ═══════════════════════════════════════════════════════════════════
 *  hwinv -- what is inside this PC, as AurOS needs to know it.
 *
 *  Stream HW (docs/handoff/streams/HW.md). Read-only: nothing here
 *  writes to a disk, a setting or the registry.
 *
 *  Three parts, in three files, so each can be tested where it lives:
 *
 *    hwinv.c         pure C. Hardware-ID parsing, "what kind of thing is
 *                    this", the inventory record, and the plain text and
 *                    JSON that `AurOS-Setup.exe --report` writes. Builds
 *                    and is unit-tested on any host (tools/hw/hwtest.sh).
 *    hwinv_win.c     Windows only. SetupAPI/CfgMgr for the devices, the
 *                    spooler for printers, the registry for the maker
 *                    and model, and the `--report` entry point. It fills
 *                    the record through hwinv_add_* below, so what the
 *                    tests classify is what the real PC is classified by.
 *    hw/lib/hwcompat.[ch]  (milestone 2) the matcher: inventory in, plain
 *                    sentences out ("Your Wi-Fi will work.").
 *
 *  PRIVACY. A report is something a person photographs, e-mails or
 *  attaches to a bug. It never carries a serial number, the PC's name,
 *  a user name, a Wi-Fi network name, an address or a file name. Printer
 *  queue names are the user's own words and may hold a person's name; the
 *  campaign sheet says so.
 * ═══════════════════════════════════════════════════════════════════ */
#ifndef AURBRIDGE_HWINV_H
#define AURBRIDGE_HWINV_H

#include <stddef.h>
#include <stdint.h>

#define HW_MAX_DEVS      384
#define HW_MAX_PRINTERS   24
#define HW_MAX_VERDICTS   32

/* The kinds she would recognise. Everything else is HW_K_OTHER and is
 * kept only when Windows itself had no driver for it (an "unknown
 * device" is exactly the hardware most likely to lack one here too). */
typedef enum {
    HW_K_OTHER = 0,
    HW_K_WIFI,
    HW_K_ETHERNET,
    HW_K_BLUETOOTH,
    HW_K_AUDIO,
    HW_K_CAMERA,
    HW_K_GPU,
    HW_K_STORAGE,       /* the controller a disk hangs off, not the disk */
    HW_K_SCANNER,
    HW_K_FINGERPRINT,
    HW_K_COUNT
} hw_kind;

typedef enum {
    HW_BUS_OTHER = 0,
    HW_BUS_PCI,
    HW_BUS_USB,
    HW_BUS_HDA,         /* an HD-Audio codec: ids are the codec's */
    HW_BUS_ACPI
} hw_bus;

/* What a hardware-ID string says, parsed. Zero fields are "not there". */
typedef struct {
    hw_bus   bus;
    int      has_ids;        /* vid/did hold real numbers                */
    uint16_t vid, did;       /* PCI VEN/DEV, USB VID/PID, HDA VEN/DEV    */
    uint32_t subsys;         /* PCI SUBSYS_ssssvvvv as one 32-bit number */
    int      rev;            /* -1 when absent                           */
    int      mi;             /* USB interface number (MI_xx), -1 absent  */
    char     acpi[16];       /* ACPI\NAME0000 -> "NAME0000"              */
    char     ieee[96];       /* USBPRINT\<maker><model>... -> that text  */
} hw_id;

typedef struct {
    hw_kind  kind;
    hw_bus   bus;
    int      has_ids;
    uint16_t vid, did;
    uint32_t subsys;
    int      rev;
    int      problem;        /* Windows' own problem code, 0 = none      */
    int      in_use;         /* network: Windows is using it right now   */
    char     cls[24];        /* Windows device class: "Net", "Display"   */
    char     name[96];       /* what Windows calls it                    */
    char     mfg[48];
    char     service[32];    /* the driver service Windows runs for it   */
    char     hwid[96];       /* the first hardware ID, verbatim          */
} hw_dev;

typedef enum {
    HW_CONN_UNKNOWN = 0,
    HW_CONN_USB,
    HW_CONN_NETWORK,
    HW_CONN_OTHER       /* parallel, serial, a file, a share            */
} hw_conn;

typedef struct {
    char     name[96];       /* the queue's name: her own words          */
    char     driver[96];     /* Windows' driver for it: names the model  */
    char     port[64];
    hw_conn  conn;
    int      is_default;
    int      has_usb;
    uint16_t vid, pid;
    char     ieee[96];       /* "HPLaserJet_Pro_M404dnC7A0"-style id     */
} hw_printer;

/* One thing she is told. `state` is the whole point; `says` is what the
 * wizard shows, in her words; `why` is for the report and for us. */
typedef enum {
    HW_V_WORKS = 0,     /* "will work"                                   */
    HW_V_PARTIAL,       /* works, with something missing                 */
    HW_V_WONT,          /* "won't work yet"                              */
    HW_V_UNKNOWN,       /* we do not know; never reported as "works"     */
    HW_V_NONE           /* this PC has none of these: nothing to say     */
} hw_vstate;

typedef struct {
    hw_kind   kind;
    hw_vstate state;
    int       dev;           /* index into inv->devs, or -1              */
    int       printer;       /* index into inv->printers, or -1          */
    int       blocks;        /* 1: AurOS cannot be used without it (the  */
                             /*    only network path she has)            */
    char      id[40];        /* stable code: "wifi-wl-only"              */
    char      says[200];     /* her sentence                             */
    char      why[240];      /* ours                                     */
} hw_verdict;

typedef struct {
    /* the maker's own words, from the firmware (SMBIOS), no serials     */
    char dmi_vendor[64], dmi_model[64], dmi_family[48], dmi_board[48];
    char bios_vendor[48], bios_version[48], bios_date[24];
    char cpu[96];
    int  cpu_threads;
    uint64_t ram_bytes;

    char os_name[64];        /* "Windows 11 Home"                        */
    char os_display[16];     /* "23H2"                                   */
    int  os_build;           /* 22631; >= 22000 is Windows 11            */
    int  is_arm;

    int  is_uefi;            /* -1 unknown                               */
    int  secure_boot;        /* -1 unknown, 0 off, 1 on                  */
    int  has_battery;        /* -1 unknown                               */

    /* the drive Windows is on */
    char     sys_disk_model[64];
    uint64_t sys_disk_bytes;
    int      sys_disk_kind;  /* -1 unknown, 0 hard disk, 1 SSD/flash     */
    int      sys_disk_bus;   /* STORAGE_BUS_TYPE, -1 unknown             */
    uint64_t sys_free_bytes;
    int      bitlocker;      /* Windows drive: -1 unknown, 0 no, 1 yes   */
    int      bitlocker_any;  /* any drive                                */

    /* the installer's own check, so a screening report says whether this
     * PC would be refused and why. -1: not run (no preflight). */
    int  pf_go;
    int  pf_blocks, pf_warns;
    struct { char id[40]; char sev[8]; char title[96]; } pf_find[16];
    int  pf_n_find;

    hw_dev     *devs;        /* HW_MAX_DEVS, heap                        */
    int         n_devs, n_dropped;
    hw_printer  printers[HW_MAX_PRINTERS];
    int         n_printers;

    hw_verdict  verdicts[HW_MAX_VERDICTS];
    int         n_verdicts;
} hw_inv;

/* ── pure: ids, kinds, the record ───────────────────────────────────── */

/* Parse a Windows hardware ID ("PCI\VEN_14E4&DEV_43B1&SUBSYS_...",
 * "USB\VID_04F9&PID_0054&MI_01", "HDAUDIO\FUNC_01&VEN_10EC&DEV_0236",
 * "ACPI\GDIX0000", "USBPRINT\Brother..."). Case-insensitive. Returns 1
 * when the bus was recognised. Never reads past a NUL. */
int  hwinv_parse_hwid(const char *hwid, hw_id *out);

/* What kind of thing is this? `cls` is Windows' device class name,
 * `if_type` is the IANA interface type of the adapter (71 = Wi-Fi,
 * 6 = Ethernet) or 0 when unknown. */
hw_kind hwinv_kind(const char *cls, const hw_id *id, const char *name,
                   const char *service, int if_type);

const char *hwinv_kind_key(hw_kind k);    /* "wifi" -- JSON, stable      */
const char *hwinv_kind_word(hw_kind k);   /* "Wi-Fi" -- for a report     */
const char *hwinv_state_key(hw_vstate s); /* "works" ...                 */

hw_inv *hwinv_new(void);
void    hwinv_free(hw_inv *);

/* Add a device as Windows reported it. Classifies it and drops what
 * nobody here needs (bridges, hubs, virtual adapters) unless Windows
 * had a problem with it. Returns the index, or -1 when dropped. */
int  hwinv_add_dev(hw_inv *, const char *cls, const char *hwid,
                   const char *name, const char *mfg, const char *service,
                   int problem, int if_type);

/* Add a printer queue. `driver` names the model; virtual printers
 * ("Microsoft Print to PDF", fax, OneNote, XPS) are dropped. Returns the
 * index or -1. */
int  hwinv_add_printer(hw_inv *, const char *name, const char *driver,
                       const char *port, int is_default);

/* Give queue `i` the USB identity of the printer it prints to. */
void hwinv_printer_usb(hw_inv *, int i, uint16_t vid, uint16_t pid,
                       const char *ieee);

/* The printers Windows found on the USB bus (its USBPRINT devices),
 * matched to the queues that print to them. Windows does not say which
 * queue is which, so: by how much of the model name they share, else by
 * "one queue, one device". A device with no queue is added as a printer
 * of its own: it is there, and Linux will see it, whether or not she
 * ever set it up in Windows. */
typedef struct { uint16_t vid, pid; char ieee[96]; int used; } hw_usbprn;
void hwinv_pair_usb_printers(hw_inv *, hw_usbprn *u, int n);

/* ── pure: text and JSON ────────────────────────────────────────────── */

/* Both return a malloc'd, NUL-terminated UTF-8 string (free()). `made`
 * is a ready-made timestamp, "tool" the program's name and version.
 * NULL only when out of memory. */
char *hwinv_text(const hw_inv *, const char *tool, const char *made);
char *hwinv_json(const hw_inv *, const char *tool, const char *made);

/* The unit test: parsing, classification, dropping, rendering. Prints
 * one line per case; returns the number wrong. */
int  hwinv_selftest(void);

/* ── Windows only (hwinv_win.c) ─────────────────────────────────────── */
#ifdef _WIN32
/* Fill `inv` from this PC. Runs no installer check itself. */
int  hwinv_collect(hw_inv *inv);

/* `--report [DIR]` / `report [DIR]`: collect, run the installer's own
 * check, judge, and write AurOS-report.txt and AurOS-report.json into
 * DIR (default: the folder the program is in, else the Desktop, else the
 * temp folder). `gui`: 1 when run from the double-clicked program with
 * no DIR -- it then says where the files are in a box. Returns 0 when
 * both files were written. Read-only toward the PC. */
int  hwinv_report_main(const char *dir, int gui, const char *tool);
#endif

#endif
