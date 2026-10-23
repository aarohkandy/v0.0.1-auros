# (f) The programs she installs: each one installs from the store's own backend with no password, shows
# up as a launcher, and opens a window within 90 seconds. Installed one after another in the same VM;
# the Flathub runtimes they share are downloaded once.
. /usr/lib/auros-parity/lib.sh
me=$(id -un)
tick() { cut -d. -f1 /proc/uptime; }

# "the store catalogue is ready within 30 seconds": from the network coming up to a search finding Zoom
n0=$(tick)
if wait_for 300 sh -c 'flatpak search --system zoom 2>/dev/null | grep -qi "zoom"'; then
    s=$(( $(tick) - n0 ))
    if [ "$s" -le 30 ]; then result apps.catalogue pass "Flathub's catalogue answered a search $s s after the check began"
    else result apps.catalogue fail "Flathub's catalogue took $s s (plan: 30 s)"; fi
else
    result apps.catalogue fail "a search for Zoom found nothing in 300 s: Flathub is not set up or unreachable"
fi
note "remotes: $(flatpak remotes --system 2>&1 | tr '\n' '|')"

grep -v '^#' "$P/apps.tsv" | while IFS="$(printf '\t')" read -r id name what; do
    [ -n "$id" ] || continue
    if [ -n "$APPS" ]; then case " $APPS " in *" $id "*|*" $name "*) ;; *) continue ;; esac; fi
    say "note ---- $name ($id)"
    t0=$(date +%s)
    out=$(timeout 1500 flatpak install --system --noninteractive -y flathub "$id" 2>&1); rc=$?
    inst=$(( $(date +%s) - t0 ))
    note "install $name rc=$rc: $(printf '%s' "$out" | tail -n 4 | tr '\n' '|' | cut -c1-300)"
    if [ "$rc" -ne 0 ]; then
        result "apps.$name" fail "did not install ($rc) after $inst s: $(printf '%s' "$out" | tail -n 2 | tr '\n' ' ' | cut -c1-240)"
        continue
    fi
    size=$(flatpak info --system "$id" 2>/dev/null | sed -n 's/^ *Installed: *//p' | head -1)
    f=/var/lib/flatpak/exports/share/applications/$id.desktop
    if [ ! -f "$f" ] || grep -qi '^NoDisplay=true' "$f"; then
        result "apps.$name" fail "installed ($size) but no launcher appeared on Home"; continue
    fi
    sleep 3; snap "apps-$name-ready"             # the shell's "is ready" card, if it showed one
    cmd=$(sed -n 's/^Exec=//p' "$f" | head -1 | sed 's/ %[A-Za-z]//g; s/@@[a-z]*//g; s# run # run --env=WAYLAND_DEBUG=1 #')
    log=/var/lib/auros-parity/app-$name.log
    : > "$log"
    snap "apps-$name-before"
    t1=$(date +%s)
    setsid sh -c "$cmd" > "$log" 2>&1 &
    wait_for 90 grep -q 'xdg_surface@[0-9]*.get_toplevel' "$log"
    win=$(( $(date +%s) - t1 ))
    if grep -q 'xdg_surface@[0-9]*.get_toplevel' "$log"; then
        sleep 12
        title=$(grep -ao 'xdg_toplevel@[0-9]*.set_title("[^"]*")' "$log" | tail -1 | sed 's/.*set_title("//; s/")$//')
        snap "apps-$name"
        say "do changed apps-$name-before apps-$name apps.$name.visible 260 130 1020 690 8"; sleep 4
        result "apps.$name" pass "installed in $inst s ($size), window \"$title\" opened $win s after the press"
    else
        note "$name log: $(head -c 600 "$log" | tr '\n' '|')"
        if [ -z "$DIAG" ]; then
            DIAG=1
            note "diag: userns restrict=$(cat /proc/sys/kernel/apparmor_restrict_unprivileged_userns 2>&1) max_userns=$(cat /proc/sys/user/max_user_namespaces 2>&1) unshare=$(unshare -Ur true 2>&1 && echo ok) bwrap=$(bwrap --ro-bind / / true 2>&1 && echo ok) bwrap_mode=$(stat -c %a /usr/bin/bwrap)"
            note "diag: flatpak run: $(flatpak run --command=true "$id" 2>&1 | head -c 400 | tr '\n' '|')"
            note "diag: kernel: $(journalctl -k --no-pager -n 400 2>&1 | grep -ai 'apparmor\|denied' | tail -n 6 | cut -c1-250 | tr '\n' '|')"
            note "diag: system: $(journalctl --no-pager -n 60 2>&1 | grep -ai 'flatpak\|bwrap\|ldconfig' | tail -n 6 | cut -c1-250 | tr '\n' '|')"
        fi
        why=$(grep -ai 'cannot open display\|unable to open display\|could not connect\|Gtk-WARNING\|error' "$log" | head -2 | tr '\n' ' ' | cut -c1-200)
        x11=""; grep -aqi 'display' "$log" && x11=" (it wants an X11 display: this desktop has no Xwayland)"
        result "apps.$name" fail "installed in $inst s ($size) but no window in 90 s$x11: ${why:-$(tail -n 2 "$log" | tr '\n' ' ' | cut -c1-200)}"
    fi
    flatpak kill "$id" 2>/dev/null; pkill -u "$me" -f "$id" 2>/dev/null; sleep 3
done
note "flatpak storage: $(du -sh /var/lib/flatpak 2>/dev/null | cut -f1); disk: $(df -h / | tail -1 | tr -s ' ')"
