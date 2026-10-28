#!/bin/sh
# inner.sh -- runs INSIDE the auros-wl container (tools/wltest-apps.sh starts it).
#   inner.sh <app> [seconds]       app: firefox | gtk4 | qt | mpv | custom:'<command>'
# Builds the real aurshell (the compositor with it, every archetype), runs it
# headless with the program on its desktop, and writes /out/<app>.png: the
# screen a person would see. Same code path as the booted machine except the
# destination (aurshell --png --with-app).
set -u
APP=${1:-firefox}; SECS=${2:-30}; SHELLF=${SHELLF:-taskbar}
cd /w/src
mkdir -p /out /tmp/run && chmod 700 /tmp/run
[ -n "${AUROS_XWM_DEBUG:-}" ] && touch /tmp/auros-xwm-debug
export XDG_RUNTIME_DIR=/tmp/run HOME=/tmp/home
mkdir -p "$HOME" "$HOME/.config/gtk-3.0" "$HOME/.config/gtk-4.0"
# Her text-size setting ("Bigger text"), if the run is to show it: 1.5 is 150%.
if [ -n "${TEXT_SCALE:-}" ]; then mkdir -p "$HOME/.config/auros" && echo "$TEXT_SCALE" > "$HOME/.config/auros/text-scale"; fi
# What build/forge writes into her home (cursor size included): programs ask
# their cursor theme for this size.
for v in 3.0 4.0; do printf '[Settings]\ngtk-cursor-theme-size=%s\n' "${CURSOR_SIZE:-48}" > "$HOME/.config/gtk-$v/settings.ini"; done

if [ ! -x /out/aurshell ] || [ -n "${REBUILD:-}" ]; then
    SRC=$(for f in aurshell/*.c aurshell/layouts/*.c; do grep -q '^int main(' "$f" && continue; printf '%s ' "$f"; done)
    # shellcheck disable=SC2086
    gcc -O1 -g -std=gnu11 -DAUROS_WISHES=0 -o /out/aurshell aurshell/main.c $SRC \
        aurwl/aurwl.c aurwl/gen/xdg-shell-protocol.c aurwl/gen/xdg-decoration-protocol.c \
        aurwl/gen/viewporter-protocol.c aurwl/gen/xdg-output-protocol.c \
        common/theme.c common/wall.c common/font.c common/png.c \
        -Iaurwl/gen $(pkg-config --cflags dbus-1) -lwayland-server -lxkbcommon $(pkg-config --libs dbus-1) -lm \
        2>&1 | tail -20
    [ -x /out/aurshell ] || { echo "aurshell did not build"; exit 1; }
fi

# A few seconds of moving test pattern, for a <video> in the page.
[ -f /out/test.webm ] || mpv --really-quiet --no-audio av://lavfi:testsrc=duration=6:size=320x180:rate=25 \
    --o=/out/test.webm --ovc=libvpx --of=webm >/dev/null 2>&1 || true

cat > /tmp/test.html <<'HTML'
<!doctype html><meta charset=utf-8><title>wltest page</title>
<body style="margin:0;font:22px sans-serif;background:#fff">
<div style="background:#c0392b;color:#fff;padding:8px">RED BAR: if you can read this, the page subsurface is on screen</div>
<a href="#top" style="display:block;background:#dfe9f5;padding:10px;font-size:24px;color:#0645ad">A LINK: the pointer should be a hand here</a>
<input type=text value="a text field: the pointer should be an I-beam here" style="font-size:24px;width:700px;margin:6px">
<div style="display:flex;align-items:flex-start">
<div style="width:120px;height:180px;background:#27ae60"></div>
<div style="width:120px;height:180px;background:#2980b9"></div>
<video src="file:///out/test.webm" width=320 height=180 autoplay muted loop style="background:#8e44ad"></video>
</div>
<p>AurOS compositor test page. 0123456789 The quick brown fox.</p>
</body>
HTML

case "$APP" in
  firefox|firefox2)         # firefox2: a second window opened a third of the way in, through Firefox's own remote command
    export MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_RDD_SANDBOX=1 MOZ_DISABLE_SOCKET_PROCESS_SANDBOX=1 MOZ_DISABLE_GMP_SANDBOX=1 MOZ_DISABLE_GPU_SANDBOX=1
    mkdir -p /tmp/ffprofile
    # A fresh profile opens a welcome overlay that hides the page under test.
    cat > /tmp/ffprofile/user.js <<'JS'
user_pref("browser.aboutwelcome.enabled", false);
user_pref("browser.preonboarding.enabled", false);
user_pref("termsofuse.bypassNotification", true);
user_pref("termsofuse.acceptedVersion", 999);
user_pref("trailhead.firstrun.didSeeAboutWelcome", true);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("toolkit.telemetry.reportingpolicy.firstRun", false);
user_pref("app.update.auto", false);
user_pref("media.autoplay.default", 0);
JS
    CMD="firefox --no-remote --new-instance --profile /tmp/ffprofile file:///tmp/test.html"
    if [ "$APP" = firefox2 ]; then
        CMD="dbus-run-session -- sh -c 'firefox --profile /tmp/ffprofile file:///tmp/test.html & sleep $((SECS / 3)); firefox --profile /tmp/ffprofile --new-window file:///tmp/test.html; wait'"
    fi ;;
  firefox-pw)       # does a password field in Firefox read as a password to the compositor?
    export MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_RDD_SANDBOX=1 MOZ_DISABLE_SOCKET_PROCESS_SANDBOX=1 MOZ_DISABLE_GMP_SANDBOX=1 MOZ_DISABLE_GPU_SANDBOX=1
    mkdir -p /tmp/ffprofile
    cp /dev/null /tmp/ffprofile/user.js
    for kv in 'browser.aboutwelcome.enabled false' 'browser.preonboarding.enabled false' 'termsofuse.bypassNotification true' \
              'trailhead.firstrun.didSeeAboutWelcome true' 'browser.shell.checkDefaultBrowser false'; do
        set -- $kv; echo "user_pref(\"$1\", $2);" >> /tmp/ffprofile/user.js; done
    printf '<!doctype html><title>pw</title><body style="font:28px sans-serif"><p>Name:</p><input id=a type=text autofocus style="font-size:28px"><p>Password:</p><input id=b type=password style="font-size:28px"></body>' > /tmp/pw.html
    gcc -O1 -g -std=gnu11 -o /out/ti-probe /w/tools/wltest-apps/ti-probe.c aurwl/aurwl.c aurwl/textinput.c aurshell/draw.c \
        aurwl/gen/xdg-shell-protocol.c aurwl/gen/xdg-decoration-protocol.c aurwl/gen/viewporter-protocol.c \
        aurwl/gen/xdg-output-protocol.c aurwl/gen/text-input-protocol.c -Iaurwl/gen \
        $(pkg-config --cflags --libs wayland-server xkbcommon) -lm || exit 1
    exec /out/ti-probe "$SECS" 14 24 -- firefox --no-remote --new-instance --profile /tmp/ffprofile file:///tmp/pw.html ;;
  thunar-dnd)       # drag a file onto a folder with the real file manager; DND_FROM / DND_TO are window points
    rm -rf /tmp/dnd && mkdir -p /tmp/dnd/box && echo hello > /tmp/dnd/a.txt && echo more > /tmp/dnd/b.txt
    gcc -O1 -g -std=gnu11 -o /out/dnd-probe /w/tools/wltest-apps/dnd-probe.c aurwl/aurwl.c aurshell/draw.c common/png.c \
        aurwl/gen/xdg-shell-protocol.c aurwl/gen/xdg-decoration-protocol.c aurwl/gen/viewporter-protocol.c \
        aurwl/gen/xdg-output-protocol.c -Iaurwl/gen $(pkg-config --cflags --libs wayland-server xkbcommon) -lm || exit 1
    /out/dnd-probe "$SECS" /out/thunar.png "${DND_FROM:-0,0}" "${DND_TO:-0,0}" -- thunar /tmp/dnd
    echo "-- after:"; ls -R /tmp/dnd
    exit 0 ;;
  cursor-probe)     # what picture does Firefox give the pointer over a link / a field / the page? (window points)
    mkdir -p /tmp/ffprofile
    cp /dev/null /tmp/ffprofile/user.js
    for kv in 'browser.aboutwelcome.enabled false' 'browser.preonboarding.enabled false' 'termsofuse.bypassNotification true' 'browser.shell.checkDefaultBrowser false'; do
        set -- $kv; echo "user_pref(\"$1\", $2);" >> /tmp/ffprofile/user.js; done
    export MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_RDD_SANDBOX=1 MOZ_DISABLE_SOCKET_PROCESS_SANDBOX=1 MOZ_DISABLE_GMP_SANDBOX=1
    gcc -O1 -g -std=gnu11 -o /out/cursor-probe /w/tools/wltest-apps/cursor-probe.c aurwl/aurwl.c aurshell/draw.c \
        aurwl/gen/xdg-shell-protocol.c aurwl/gen/xdg-decoration-protocol.c aurwl/gen/viewporter-protocol.c \
        aurwl/gen/xdg-output-protocol.c -Iaurwl/gen $(pkg-config --cflags --libs wayland-server xkbcommon) -lm || exit 1
    exec /out/cursor-probe "$SECS" ${PROBE_POINTS:-300,320 400,380 900,500} -- firefox --no-remote --new-instance --profile /tmp/ffprofile file:///tmp/test.html ;;
  firefox-stall)    # Firefox a second behind while the compositor reconfigures its window a dozen times; it answers every configure in order
    export MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_RDD_SANDBOX=1 MOZ_DISABLE_SOCKET_PROCESS_SANDBOX=1 MOZ_DISABLE_GMP_SANDBOX=1 MOZ_DISABLE_GPU_SANDBOX=1
    mkdir -p /tmp/ffprofile; cp /dev/null /tmp/ffprofile/user.js
    for kv in 'browser.aboutwelcome.enabled false' 'browser.preonboarding.enabled false' 'termsofuse.bypassNotification true' 'browser.shell.checkDefaultBrowser false'; do
        set -- $kv; echo "user_pref(\"$1\", $2);" >> /tmp/ffprofile/user.js; done
    gcc -O1 -g -std=gnu11 -o /out/stall-probe /w/tools/wltest-apps/stall-probe.c aurwl/aurwl.c aurshell/draw.c \
        aurwl/gen/xdg-shell-protocol.c aurwl/gen/xdg-decoration-protocol.c aurwl/gen/viewporter-protocol.c \
        aurwl/gen/xdg-output-protocol.c -Iaurwl/gen $(pkg-config --cflags --libs wayland-server xkbcommon) -lm || exit 1
    exec /out/stall-probe "$SECS" firefox-bin -- firefox --no-remote --new-instance --profile /tmp/ffprofile file:///tmp/test.html ;;
  flatpak-install)  # FPREF=org.videolan.VLC: download it (and its runtime) into /fp; nothing is drawn
    flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
    exec flatpak install -y --noninteractive flathub ${FPREF:?set FPREF} ;;
  flatpak)          # FPREF=org.videolan.VLC [FPARGS=--nosocket=wayland] [FPAPPARGS='args to the program']: an installed Flatpak on the real shell, through X11
    mkdir -p /run/dbus && rm -f /run/dbus/pid && dbus-daemon --system --fork      # flatpak talks to a system bus, and to a session bus
    case "$FPREF" in    # first-run questions that would sit modal in front of what is under test
    org.videolan.VLC) d=$HOME/.var/app/org.videolan.VLC/config/vlc; mkdir -p "$d"
                      printf '[qt]\nqt-privacy-ask=0\n[core]\nmetadata-network-access=0\n' > "$d/vlcrc" ;;
    esac
    CMD="dbus-run-session -- flatpak run ${FPARGS:-} ${FPREF:?set FPREF} ${FPAPPARGS:-}" ;;
  clip)             # the clipboard between an X program and a Wayland one (clip-probe.c): FPIMAGE=1, files in /out
    rm -f /out/x-reads.txt /out/wl-reads.txt
    mkdir -p /run/dbus && rm -f /run/dbus/pid && dbus-daemon --system --fork
    gcc -O1 -g -std=gnu11 -o /out/clip-probe /w/tools/wltest-apps/clip-probe.c aurwl/aurwl.c aurshell/draw.c \
        aurwl/gen/xdg-shell-protocol.c aurwl/gen/xdg-decoration-protocol.c aurwl/gen/viewporter-protocol.c \
        aurwl/gen/xdg-output-protocol.c -Iaurwl/gen $(pkg-config --cflags --libs wayland-server xkbcommon) -lm || exit 1
    rm -f /out/x-image.png /out/wl-image.png /out/*-image.err
    IMG=$(find /out -name '*.png' -size +600k | head -1)
    /out/clip-probe /out $IMG
    for f in x-reads wl-reads; do printf '%s: ' "$f"; cat "/out/$f.txt" 2>/dev/null; echo; done
    # The clipboard follows the keyboard: xclip has no window, so it is refused both ways (clip-probe.c says why).
    # x-reads must not hold the Wayland copy; wl-reads is the Wayland program's own copy, never xclip's.
    if grep -q from-wayland /out/x-reads.txt 2>/dev/null; then echo "PRIVACY FAIL: an X client with no window with the keyboard read the Wayland copy"
    elif grep -q from-x /out/wl-reads.txt 2>/dev/null; then echo "PRIVACY FAIL: an X client with no window with the keyboard replaced the Wayland copy"
    else echo "PRIVACY OK: xclip read nothing of the Wayland copy and its own copy did not reach the Wayland program"; fi
    if [ -n "$IMG" ]; then
        ls -l "$IMG" /out/x-image.png /out/wl-image.png 2>&1 | sed 's/^/  /'
        for f in x wl; do cmp -s "$IMG" "/out/$f-image.png" && echo "$f-image: PRIVACY FAIL, the picture crossed" || echo "$f-image: did not cross (refused, as it must be)"; done
    fi
    exit 0 ;;
  click)            # click points in a program's window and show what it was sent (run with WAYLAND_DEBUG=client);
                    # CLICKS="x,y x,y" (window points), XCMD="the program"; for the store: flatpak image, see GS.md notes in WL.md
    gcc -O1 -g -std=gnu11 -o /out/click-probe /w/tools/wltest-apps/click-probe.c aurwl/aurwl.c aurshell/draw.c common/png.c \
        aurwl/gen/xdg-shell-protocol.c aurwl/gen/xdg-decoration-protocol.c aurwl/gen/viewporter-protocol.c \
        aurwl/gen/xdg-output-protocol.c -Iaurwl/gen $(pkg-config --cflags --libs wayland-server xkbcommon) -lm || exit 1
    exec /out/click-probe "$SECS" /out/click.png ${CLICKS:-} -- ${XCMD:?set XCMD} ;;
  xterm)  CMD="xterm -fa Monospace -fs 14 -e sh -c 'echo X11 via Xwayland; sleep 600'" ;;
  xeyes)  CMD="xeyes" ;;
  x11-firefox)                                  # a big GTK3 program, forced onto X11
    mkdir -p /tmp/ffprofile; cp /dev/null /tmp/ffprofile/user.js
    for kv in 'browser.aboutwelcome.enabled false' 'browser.preonboarding.enabled false' 'termsofuse.bypassNotification true' 'browser.shell.checkDefaultBrowser false'; do
        set -- $kv; echo "user_pref(\"$1\", $2);" >> /tmp/ffprofile/user.js; done
    CMD="GDK_BACKEND=x11 MOZ_ENABLE_WAYLAND=0 MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_RDD_SANDBOX=1 MOZ_DISABLE_SOCKET_PROCESS_SANDBOX=1 firefox --no-remote --new-instance --profile /tmp/ffprofile file:///tmp/test.html" ;;
  qt-xcb) CMD="QT_QPA_PLATFORM=xcb qterminal" ;;
  xinput)           # type into a real X program through the real compositor; the program writes what it got to /out/xinput.txt
    rm -f /out/xinput.txt
    gcc -O1 -g -std=gnu11 -o /out/input-probe /w/tools/wltest-apps/input-probe.c aurwl/aurwl.c aurshell/draw.c \
        aurwl/gen/xdg-shell-protocol.c aurwl/gen/xdg-decoration-protocol.c aurwl/gen/viewporter-protocol.c \
        aurwl/gen/xdg-output-protocol.c -Iaurwl/gen $(pkg-config --cflags --libs wayland-server xkbcommon) -lm || exit 1
    printf 'read line\necho "got: $line" > /out/xinput.txt\nsleep 600\n' > /tmp/xi.sh
    if [ -n "${XCMD:-}" ]; then exec /out/input-probe "$SECS" "${KEYS:-hello 42\n}" -- $XCMD; fi
    exec /out/input-probe "$SECS" "${KEYS:-hello 42\n}" -- xterm -fa Monospace -fs 14 -T xinput-test -e sh /tmp/xi.sh ;;
  gtk4) CMD="GSK_RENDERER=cairo gtk4-widget-factory" ;;
  gtk4gl) CMD="gtk4-widget-factory" ;;
  qt)   CMD="qterminal" ;;
  mpv)  CMD="mpv --vo=wlshm --no-audio --really-quiet --loop av://lavfi:testsrc=size=640x360:rate=25" ;;
  custom:*) CMD=${APP#custom:} ;;
  *) echo "unknown app $APP"; exit 2 ;;
esac
echo "== $APP: $CMD"
# SCLICKS="x,y x,y": screen points the shell's own pointer path clicks (aurshell --click);
# STYPE="text": typed through the shell's own key path after the clicks (aurshell --type)
CK=""; for pt in ${SCLICKS:-}; do CK="$CK --click ${pt%,*} ${pt#*,}"; done
# shellcheck disable=SC2086
/out/aurshell --shell "/w/shells/$SHELLF.shell" $CK ${STYPE:+--type "$STYPE"} --png "/out/$(echo "$APP" | tr -c 'a-z0-9\n' _).png" \
    --size 1280 800 ${MOUSE:+--mouse $MOUSE} --app-wait "$SECS" --with-app "$CMD" 2>&1 | grep -a -v "Could not resolve keysym" | tail -${TAIL:-15}
