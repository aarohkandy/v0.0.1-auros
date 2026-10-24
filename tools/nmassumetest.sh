#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  nmassumetest -- does the cable work when the card arrives configured?
#
#  After a real install AurOS's first session is the staging kernel
#  handing over in the SAME boot: the card is already up (and the kernel
#  has given it an IPv6 address) when NetworkManager starts. Left alone
#  NetworkManager "assumes" it, never runs DHCP, and sits in
#  CONNECTED_LOCAL with no default route: no clock from the network,
#  no Flathub, an empty store (RC1, RC6; run 40050260949).
#
#  This boots a COPY of the image under KVM, user-mode network, with a
#  one-shot unit before NetworkManager that brings the card up the way
#  staging leaves it, twice:
#
#    as the image is                  must reach CONNECTED_GLOBAL, hold a
#                                     DHCP lease and a default route, and
#                                     (software-ready.sh) fetch the catalogue
#    without 20-auros-fresh.conf      must reach CONNECTED_LOCAL: the test
#                                     can see the difference (red without)
#
#      sh tools/nmassumetest.sh IMAGE.img       ~5 minutes, 5 GB of memory
#
#  Linux, docker (--privileged for loop devices; /dev/kvm), python3, sfdisk.
#  KVM_IMAGE (default a2-kvm:latest) is a docker image with qemu and OVMF.
#  The image is not modified. Needs ~6 GB of scratch in $TMPDIR per copy.
# ═══════════════════════════════════════════════════════════════════
set -eu
IMG=${1:?usage: nmassumetest.sh IMAGE.img}
IMG=$(cd "$(dirname "$IMG")" && pwd)/$(basename "$IMG")
KVM_IMAGE=${KVM_IMAGE:-a2-kvm:latest}
W=$(mktemp -d "${TMPDIR:-/tmp}/nmassume.XXXXXX"); trap 'rm -rf "$W"' EXIT INT TERM
read -r OFF SIZE <<EOP
$(sfdisk -J "$IMG" | python3 -I -c '
import json,sys
p=[x for x in json.load(sys.stdin)["partitiontable"]["partitions"] if x.get("name")=="AUROS-ROOT"][0]
print(p["start"]*512, p["size"]*512)')
EOP

cat > "$W/prep.sh" <<'EOS'
set -eu
LD=$(losetup -f --show -o "$OFF" --sizelimit "$SIZE" /w/disk.img)
mkdir -p /r; mount "$LD" /r
[ "$VARIANT" = without ] && rm -f /r/etc/NetworkManager/conf.d/20-auros-fresh.conf
cat > /r/etc/systemd/system/fake-staging.service <<"EOU"
[Unit]
Description=what the staging kernel leaves: the card up, nobody ran DHCP
Wants=sys-subsystem-net-devices-enp0s1.device
After=sys-subsystem-net-devices-enp0s1.device
Before=NetworkManager.service
[Service]
Type=oneshot
ExecStart=/usr/sbin/ip link set enp0s1 up
ExecStart=/bin/sleep 3
[Install]
WantedBy=multi-user.target
EOU
ln -sf /etc/systemd/system/fake-staging.service /r/etc/systemd/system/multi-user.target.wants/
cat > /r/usr/local/bin/nmdiag.sh <<"EOU"
#!/bin/sh
exec >>/var/log/nmdiag.log 2>&1
sleep 80
echo "STATE $(nmcli -t -f STATE general)"
echo "LEASE $(ip -4 addr show enp0s1 | grep -c 'dynamic')"
echo "ROUTE $(ip route show default | grep -c .)"
echo "CATALOGUE $(ls /var/lib/auros 2>/dev/null | grep -c '^catalogue-ready$')"
sync; systemctl poweroff
EOU
chmod +x /r/usr/local/bin/nmdiag.sh
printf '[Unit]\nAfter=multi-user.target\n[Service]\nExecStart=/usr/local/bin/nmdiag.sh\n[Install]\nWantedBy=multi-user.target\n' > /r/etc/systemd/system/nmdiag.service
ln -sf /etc/systemd/system/nmdiag.service /r/etc/systemd/system/multi-user.target.wants/
umount /r; losetup -d "$LD"
EOS
cat > "$W/boot.sh" <<'EOS'
cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/vars.fd
timeout 330 qemu-system-x86_64 -name nmassume -nodefaults -machine q35,accel=kvm -cpu host -smp 4 -m 4096 \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,unit=1,file=/tmp/vars.fd \
  -drive id=d0,if=none,file=/w/disk.img,format=raw,cache=unsafe -device ide-hd,drive=d0,bus=ide.0,bootindex=1 \
  -netdev user,id=n0 -device e1000e,netdev=n0,romfile= -device VGA -rtc base=utc,driftfix=slew \
  -serial file:/w/serial.log -display none
EOS
cat > "$W/read.sh" <<'EOS'
LD=$(losetup -f --show -r -o "$OFF" --sizelimit "$SIZE" /w/disk.img)
mkdir -p /r; mount -o ro,noload "$LD" /r; cat /r/var/log/nmdiag.log > /w/result.txt 2>&1; umount /r; losetup -d "$LD"
EOS

boot() { # VARIANT -> prints STATE LEASE ROUTE CATALOGUE
    cp --sparse=always "$IMG" "$W/disk.img"; chmod u+w "$W/disk.img"
    docker run --rm --privileged -v "$W":/w -e OFF="$OFF" -e SIZE="$SIZE" -e VARIANT="$1" ubuntu:24.04 bash /w/prep.sh
    docker run --rm --device /dev/kvm --memory 5g -v "$W":/w "$KVM_IMAGE" sh /w/boot.sh
    docker run --rm --privileged -v "$W":/w -e OFF="$OFF" -e SIZE="$SIZE" ubuntu:24.04 bash /w/read.sh
    tr '\n' ' ' < "$W/result.txt"; rm -f "$W/disk.img" "$W/result.txt"
}
fail=0
echo "  as the image is (the card up, nobody ran DHCP)"
r=$(boot with); echo "    $r"
case "$r" in *"STATE connected"*"LEASE 1"*"ROUTE 1"*"CATALOGUE 1"*) echo "    CONNECTED_GLOBAL, a lease, a default route, the catalogue fetched   ok" ;;
             *) echo "    FAIL: not online after a staging handover"; fail=1 ;; esac
echo "  without 20-auros-fresh.conf (the test must see the difference)"
r=$(boot without); echo "    $r"
case "$r" in *"STATE connected (local only)"*|*"ROUTE 0"*) echo "    CONNECTED_LOCAL, no route: red, as it must be   ok" ;;
             *) echo "    FAIL: the test is green without the fix, so it proves nothing"; fail=1 ;; esac
[ "$fail" = 0 ] && echo "nmassumetest: online after the handover, and red without the fix" || { echo "nmassumetest: FAILED"; exit 1; }
