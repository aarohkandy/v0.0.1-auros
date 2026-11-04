# othersys P1 results

| ok | phase | check | expected | detail |
|---|---|---|---|---|
| yes | install | the firmware started the ISO's shim from the BootNext entry | pass |  |
| yes | install | the early check matched the disk Windows recorded | pass |  |
| yes | install | the installer finished and switched off | pass |  |
| yes | install | Windows' partitions byte-identical, GPT entries unchanged | pass |  |
| yes | install | no MOK enrolment queued (MokNew/MokAuth/... absent) | pass |  |
| yes | install | Ubuntu has one entry, on its own EFI partition | pass |  |
| yes | install | the "AurOS Installer" restart entry is gone | pass |  |
| yes | install | BootOrder starts Windows first | pass |  |
| yes | install | BootNext is Ubuntu (the hold) | pass |  |
| yes | install | the helper recorded the install in the new system | pass |  |
| **NO** | firstboot | Ubuntu started from its own disk and signed her in | pass | "OTHERSYS-TEST SESSION-UP" never appeared on the serial port |
| yes | firstboot | no MokManager (blue) screen in any of 81 screenshots | pass |  |
| yes | firstboot | Windows' partitions byte-identical, GPT entries unchanged | pass |  |
| **NO** | firstboot | the temporary ISO partition is gone | pass | partition 7 is still the ISO partition (AUROS-OS-ISO) |
| **NO** | firstboot | Ubuntu got the ISO's space | pass | the Ubuntu partition did not get the ISO's space (58720256 sectors) |
| yes | firstboot | no MOK enrolment queued (MokNew/MokAuth/... absent) | pass |  |
| yes | firstboot | Ubuntu has one entry, on its own EFI partition | pass |  |
| yes | firstboot | the "AurOS Installer" restart entry is gone | pass |  |
| yes | firstboot | BootOrder starts Windows first | pass |  |
| **NO** | firstboot | BootNext is Ubuntu (the hold) | pass | BootNext is None, expected 0004 |
| yes | windowsfirst | without BootNext, the firmware starts Windows (BootOrder) | pass |  |
| yes | windowsfirst | and Ubuntu did not start | pass |  |
| yes | confirm | Ubuntu started and took the answer | pass |  |
| yes | confirm | no MOK enrolment queued (MokNew/MokAuth/... absent) | pass |  |
| yes | confirm | Ubuntu has one entry, on its own EFI partition | pass |  |
| yes | confirm | the "AurOS Installer" restart entry is gone | pass |  |
| **NO** | confirm | BootOrder starts Ubuntu first, Windows kept | pass | BootOrder does not start with Ubuntu 0004: 0002,0000,0001,0004 |
| yes | confirm | her answer is in the firmware for Windows to read | pass |  |
| yes | confirm | Windows' partitions byte-identical, GPT entries unchanged | pass |  |
| yes | confirm | no MokManager (blue) screen in any of 9 screenshots | pass |  |
| **NO** | afteryes | without BootNext, the firmware now starts Ubuntu | pass | "OTHERSYS-TEST SESSION-UP" never appeared on the serial port |
| yes | afteryes | no MokManager (blue) screen in any of 4 screenshots | pass |  |
| yes | afteryes | Windows' partitions byte-identical, GPT entries unchanged | pass |  |
| yes | moknegative | the firmware check sees a planted MOK request (must fail) | fail | MokManager has work queued for the next start: MokNew, MokAuth |
| **NO** | moknegative | the screen check sees MokManager's blue screen (must fail) | fail |  |

## Timings (wall clock)

| phase | what | seconds |
|---|---|---|
| install | BootNext to power off (whole unattended install) | 425.2 |
| firstboot | power on to the desktop session (signed in by itself) | 1200.7 |
| windowsfirst | power on to Windows Boot Manager | 2.1 |
| confirm | power on to the desktop session | 66.3 |
| afteryes | power on to the desktop session | 10.2 |
