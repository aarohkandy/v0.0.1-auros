# Fast-path promotion record

Prepared by the AurOS agents without a lawyer (D15). The public wording that waited here is now in Terms version 4,
Privacy version 6 and the website beside the default-fast-path switch. It describes the implementation after the
Restart-now Wi-Fi boundary and cleanup tests in `tools/wifitest.sh`.

Keep these constraints when the wording changes:

- Do not promise how many boots BitLocker protection stays paused. `phases.c` requests `-RebootCount 2`, but Windows'
  observed protection state is the authority. Say that it stays paused until Windows reports otherwise and tell the
  person to start Windows again.
- Say that the Wi-Fi copy on the start-up partition is not encrypted, is never sent over the internet, and is deleted
  when AurOS reads it whether or not AurOS can use it.
- Do not say every Wi-Fi can be copied. Unsupported and unreadable profiles continue installing and ask the person to
  connect from AurOS.
- The default-fast-path installer keeps the compressed download on Windows while it prepares AurOS; it does not first
  unpack another roughly 5 GB image file there.
