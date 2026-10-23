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
# Evidence for the report: what first boot did to the disk (it runs before the desktop).
cp /var/log/auros-firstboot.log /var/lib/auros-parity/firstboot.log 2>/dev/null
chmod 644 /var/lib/auros-parity/firstboot.log 2>/dev/null
exit 0
