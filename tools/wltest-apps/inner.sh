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
export XDG_RUNTIME_DIR=/tmp/run HOME=/tmp/home
mkdir -p "$HOME"

if [ ! -x /out/aurshell ] || [ -n "${REBUILD:-}" ]; then
    SRC=$(for f in aurshell/*.c aurshell/layouts/*.c; do grep -q '^int main(' "$f" && continue; printf '%s ' "$f"; done)
    # shellcheck disable=SC2086
    gcc -O1 -g -std=gnu11 -DAUROS_WISHES=0 -o /out/aurshell aurshell/main.c $SRC \
        aurwl/aurwl.c aurwl/gen/xdg-shell-protocol.c aurwl/gen/xdg-decoration-protocol.c \
        aurwl/gen/viewporter-protocol.c aurwl/gen/xdg-output-protocol.c \
        $(for g in aurwl/gen/*-protocol.c; do case $g in */xdg-shell-*|*/xdg-decoration-*|*/viewporter-*|*/xdg-output-*|*/text-input-*) ;; *) echo $g;; esac; done) \
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
<body style="margin:0;font:28px sans-serif;background:#fff">
<div style="background:#c0392b;color:#fff;padding:12px">RED BAR: if you can read this, the page subsurface is on screen</div>
<div style="display:flex"><div style="width:200px;height:200px;background:#27ae60"></div>
<div style="width:200px;height:200px;background:#2980b9"></div>
<div style="width:200px;height:200px;background:#f1c40f"></div></div>
<p>AurOS compositor test page. 0123456789 The quick brown fox jumps over the lazy dog.</p>
<video src="file:///out/test.webm" width=320 height=180 autoplay muted loop style="background:#8e44ad"></video>
</body>
HTML

case "$APP" in
  firefox)
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
    CMD="firefox --no-remote --new-instance --profile /tmp/ffprofile file:///tmp/test.html" ;;
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
  gtk4) CMD="GSK_RENDERER=cairo gtk4-widget-factory" ;;
  gtk4gl) CMD="gtk4-widget-factory" ;;
  qt)   CMD="qterminal" ;;
  mpv)  CMD="mpv --vo=wlshm --no-audio --really-quiet --loop av://lavfi:testsrc=size=640x360:rate=25" ;;
  custom:*) CMD=${APP#custom:} ;;
  *) echo "unknown app $APP"; exit 2 ;;
esac
echo "== $APP: $CMD"
/out/aurshell --shell "/w/shells/$SHELLF.shell" --png "/out/$(echo "$APP" | tr -c 'a-z0-9\n' _).png" \
    --size 1280 800 --app-wait "$SECS" --with-app "$CMD" 2>&1 | tail -15
