#!/bin/sh
# NOT PART OF AUROS. Runs as root before the desktop starts (ExecStartPre=+).
# The few things a person's hardware does by itself: a camera exists, and the
# test can talk on the serial port.
chmod 666 /dev/ttyS0 2>/dev/null
mkdir -p /var/lib/auros-parity /var/lib/auros-parity/out
u=$(sed -n 's/^default_user *= *//p' /etc/auros/policy.conf | tr -d ' ')
chown -R "${u:-auros}" /var/lib/auros-parity
# vivid is the kernel's own virtual webcam (the image blacklists it: auros-hardening.conf, so --ignore-install): a V4L2 capture device that shows
# a test picture, no feeder program needed. (v4l2loopback is in the kernel
# too, but needs ffmpeg or gstreamer to feed it, and the image has neither.)
{ modprobe -v --ignore-install vivid n_devs=1 node_types=0x1; echo rc=$?; ls -l /dev/video*; dmesg | tail -15; } > /var/lib/auros-parity/modprobe.log 2>&1
chmod 644 /var/lib/auros-parity/modprobe.log
# The red proof: remove a package BEFORE the desktop starts, so the check that
# depends on it has to fail (the overlay is thrown away afterwards).
. /usr/lib/auros-parity/config
for p in $WITHOUT; do
    dpkg --remove --force-depends --force-remove-essential "$p" >> /var/lib/auros-parity/without.log 2>&1
done
# What the image SHOULD have carried (--apt PKG): installed from Ubuntu's main, so a check that fails
# today can be shown green once the package ships (the overlay is thrown away afterwards).
if [ -n "$APT" ]; then
    printf 'deb http://archive.ubuntu.com/ubuntu noble main\ndeb http://archive.ubuntu.com/ubuntu noble-updates main\ndeb http://security.ubuntu.com/ubuntu noble-security main\n' > /usr/lib/auros-parity/parity.list
    AO="-o Dir::Etc::sourcelist=/usr/lib/auros-parity/parity.list -o Dir::Etc::sourceparts=/nonexistent -o Acquire::Languages=none -o Acquire::Retries=3"
    i=0; until nm-online -q -t 5 || [ "$i" -ge 12 ]; do i=$((i+1)); done
    { timeout 300 apt-get $AO update -q && DEBIAN_FRONTEND=noninteractive timeout 1500 apt-get $AO install -y -q --no-install-recommends $APT; echo "apt rc=$?"; } > /var/lib/auros-parity/apt.log 2>&1
    chmod 644 /var/lib/auros-parity/apt.log
fi

# (c) printing: another machine on the home network with an IPP Everywhere printer. The image's
# avahi never publishes, so the printer cannot live on the computer itself: a network namespace
# joined by a veth pair plays the other machine (mdns_printer.py + ippeveprinter inside it).
case " $CHECKS " in *" print "*)
    PARITY_USER="${u:-auros}"; echo "PARITY_USER=$PARITY_USER" >> /usr/lib/auros-parity/config
    ip netns add parityprn && ip link add vparity0 type veth peer name vparity1 &&
    ip link set vparity1 netns parityprn && ip addr add 192.168.77.1/24 dev vparity0 && ip link set vparity0 up &&
    ip netns exec parityprn sh -c 'ip addr add 192.168.77.2/24 dev vparity1; ip link set vparity1 up; ip link set lo up' &&
    systemd-run -q -u parity-printer ip netns exec parityprn sh /usr/lib/auros-parity/printer-ns.sh
    echo "printer namespace rc=$?" >> /var/lib/auros-parity/without.log ;;
esac

# Evidence for the report: what first boot did to the disk (it runs before the desktop).
cp /var/log/auros-firstboot.log /var/lib/auros-parity/firstboot.log 2>/dev/null
chmod 644 /var/lib/auros-parity/firstboot.log 2>/dev/null
exit 0
