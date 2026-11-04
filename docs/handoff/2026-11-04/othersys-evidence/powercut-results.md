# othersys P1 results

| ok | phase | check | expected | detail |
|---|---|---|---|---|
| yes | powercut | the installer put Ubuntu first and the guard put Windows back | pass |  |
| **NO** | powercut | the cut came before the install finished | pass | the install had already finished |
| yes | powercut | at the cut: BootOrder starts Windows first | pass |  |
| **NO** | powercut | at the cut: no BootNext | pass | BootNext 0004 |
| yes | powercut | Windows' partitions byte-identical, GPT entries unchanged | pass |  |
| **NO** | afterpowercut | the next start is Windows | pass | "WINDOWS-STAND-IN-BOOTED" never appeared on the serial port |
| yes | afterpowercut | and not the half-installed Ubuntu | pass |  |

## Timings (wall clock)

| phase | what | seconds |
|---|---|---|
| powercut | power on to the cut | 475.4 |
| afterpowercut | power on to Windows Boot Manager | 103.7 |
