# othersys P1 results

| ok | phase | check | expected | detail |
|---|---|---|---|---|
| yes | install | the firmware started the ISO's shim from the BootNext entry | pass |  |
| **NO** | install | the early check matched the disk Windows recorded | pass | "othersys: early: disk" never appeared on the serial port |
| **NO** | install | the installer finished and switched off | pass | "OTHERSYS-TEST LATE-DONE" never appeared on the serial port; still running at the deadline |
| yes | install | Windows' partitions byte-identical, GPT entries unchanged | pass |  |
| yes | install | no MOK enrolment queued (MokNew/MokAuth/... absent) | pass |  |
| **NO** | install | Ubuntu has one entry, on its own EFI partition | pass | expected one entry for \EFI\ubuntu\shimx64.efi on Ubuntu's own EFI partition, found none |
| **NO** | install | the "AurOS Installer" restart entry is gone | pass | Boot0003 "AurOS Installer" is still there |
| yes | install | BootOrder starts Windows first | pass |  |

## Timings (wall clock)

| phase | what | seconds |
|---|---|---|
| install | BootNext to power off (whole unattended install) | 4800.5 |
