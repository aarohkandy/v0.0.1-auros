#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  AurOS first boot: what the person chose in the installer
#
#  The installer (AurBridge) asks for a language, a keyboard, a time
#  zone, a look and a way of working, and for a long time dropped every
#  answer but one: the journal it hands the staging environment carries
#  only the profile. So a person who picked "Español", a Spanish
#  keyboard and the Moss look got an American desktop in Nocturne.
#
#  AurBridge now writes the answers to \EFI\AurOS\choices.conf on this
#  machine's EFI partition, beside the installer it started -- the one
#  place both sides of the restart can reach and the install leaves as
#  it found it. This finds that file, checks every value against what
#  this system actually has, applies what passes, and records what it
#  applied in /etc/auros/installer-choices.conf so that Ferry, which
#  later copies settings across from Windows, does not overwrite a
#  choice somebody made on purpose with the default they were offered.
#
#  THE FILE IS INPUT, NOT CODE. It is never sourced. Each value is
#  matched against a pattern before it is used, and a value that fails
#  is reported and skipped; nothing here can make the boot fail.
#
#    choices.conf, as AurBridge writes it:
#
#      language=es_ES.UTF-8
#      keyboard=xkb:es            or  keyboard=klid:0000040A
#      timezone=iana:Europe/Madrid or  timezone=windows:Romance Standard Time
#      rtc=utc                    (optional: Windows keeps the PC's clock in UTC)
#      theme=moss
#      shell=taskbar
#
#  For tests: AUROS_CHOICES_FILE names the file (skipping the search),
#  and AUROS_CHOICES_ROOT is a directory standing in for / -- in which
#  case nothing is run, only files are written (tools/choicestest.sh).
# ═══════════════════════════════════════════════════════════════════
set -u
ROOT="${AUROS_CHOICES_ROOT:-}"
SHARE="${FERRY_SHARE:-$ROOT/usr/share/ferry}"
ESP_TYPE="c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
OUT="$ROOT/etc/auros/installer-choices.conf"

say() { printf 'choices: %s\n' "$*"; }

# ── find it ────────────────────────────────────────────────────────
#
# Every EFI System partition on every disk, mounted READ-ONLY one at a
# time, until one holds \EFI\AurOS\choices.conf. AurOS's own start-up
# partition is one of them and holds no such file, so it is passed over
# by what is on it rather than by a name somebody could change.
find_choices() {
    if [ -n "${AUROS_CHOICES_FILE:-}" ]; then
        [ -f "$AUROS_CHOICES_FILE" ] && cat "$AUROS_CHOICES_FILE"
        return
    fi
    command -v lsblk >/dev/null 2>&1 || return
    mnt=$(mktemp -d /run/auros-choices.XXXXXX 2>/dev/null) || return
    lsblk -rpno NAME,PARTTYPE 2>/dev/null | while read -r dev type; do
        [ "$type" = "$ESP_TYPE" ] || continue
        mount -o ro,nodev,nosuid,noexec -t vfat "$dev" "$mnt" 2>/dev/null || continue
        # vfat looks names up without regard to case, so one spelling
        # finds the file however the firmware or Windows wrote it.
        f="$mnt/EFI/AurOS/choices.conf"
        if [ -f "$f" ]; then
            # Bounded, printable, and nothing else: a FAT file anybody
            # with Windows could have written is data to be checked.
            head -c 4096 "$f" | tr -cd '\n\040-\176'
            umount "$mnt" 2>/dev/null
            break
        fi
        umount "$mnt" 2>/dev/null
    done
    rmdir "$mnt" 2>/dev/null
}

TEXT=$(find_choices)
if [ -z "$TEXT" ]; then
    say "no installer choices on this machine; keeping the image's defaults"
    exit 0
fi

get() { # key -> value of the LAST line for it, or nothing
    printf '%s\n' "$TEXT" | sed -n "s/^$1=//p" | tail -n 1
}
matches() { printf '%s' "$1" | grep -Eq "^($2)\$"; }

mkdir -p "$ROOT/etc/auros"
: > "$OUT.new"
record() { printf '%s=%s\n' "$1" "$2" >> "$OUT.new"; }

# ── language ───────────────────────────────────────────────────────
#
# A glibc locale name, and only one this system knows how to build. It
# is generated here if it is not already: a LANG naming a locale that
# does not exist is silently the C locale, which is what every image
# had before the locales package was added -- en_US.UTF-8 in
# /etc/default/locale, and nothing behind it.
v=$(get language)
if [ -n "$v" ]; then
    if matches "$v" '[a-z]{2,3}(_[A-Z]{2})?(@[a-z]+)?\.UTF-8' &&
       { [ ! -f "$ROOT/usr/share/i18n/SUPPORTED" ] ||
         grep -q "^$v UTF-8\$" "$ROOT/usr/share/i18n/SUPPORTED"; }; then
        grep -q "^$v UTF-8\$" "$ROOT/etc/locale.gen" 2>/dev/null ||
            printf '%s UTF-8\n' "$v" >> "$ROOT/etc/locale.gen"
        if [ -z "$ROOT" ] && command -v locale-gen >/dev/null 2>&1; then
            locale-gen >/dev/null 2>&1 || say "locale-gen did not finish; $v may not be complete"
        fi
        printf 'LANG=%s\n' "$v" > "$ROOT/etc/default/locale"
        printf 'LANG=%s\n' "$v" > "$ROOT/etc/locale.conf"
        record language "$v"
        say "language: $v"
    else
        say "language '$v' is not one this system has; left as it was"
    fi
fi

# ── keyboard ───────────────────────────────────────────────────────
#
# Into /etc/default/keyboard, which is the file aurwl reads (see
# kb_defaults in src/aurwl/aurwl.c) -- and the console, and everything
# else. A Windows keyboard id (KLID) is mapped with Ferry's own table,
# exactly as ferry-settings maps it, so the two cannot disagree.
v=$(get keyboard)
layout="" variant=""
case "$v" in
    xkb:*)
        lv=${v#xkb:}
        layout=${lv%%:*}; variant=""
        [ "$lv" != "$layout" ] && variant=${lv#*:}
        ;;
    klid:*)
        klid=$(printf '%s' "${v#klid:}" | tr 'a-f' 'A-F')
        if matches "$klid" '[0-9A-F]{8}'; then
            hit=$(awk -F'\t' -v k="$klid" '/^#/ || NF < 2 { next }
                  $1 == k { print $2 (NF>=3 && $3!=" " ? "\t" $3 : ""); exit }' \
                  "$SHARE/klid.tab" 2>/dev/null)
            if [ -z "$hit" ]; then
                low=$(printf '%s' "$klid" | cut -c5-8)
                hit=$(awk -F'\t' -v l="$low" '/^#/ || NF < 2 { next }
                      substr($1, 5) == l { print $2; exit }' "$SHARE/klid.tab" 2>/dev/null)
            fi
            layout=$(printf '%s' "$hit" | cut -f1)
            variant=$(printf '%s' "$hit" | cut -s -f2)
            [ "$variant" = "$layout" ] && variant=""
        fi
        ;;
esac
if [ -n "$v" ]; then
    if [ -n "$layout" ] && matches "$layout" '[a-z]{2,8}' &&
       { [ -z "$variant" ] || matches "$variant" '[a-z0-9_-]{1,24}'; }; then
        kb="$ROOT/etc/default/keyboard"
        {
            echo '# Written at first boot from the keyboard chosen in the installer.'
            grep -E '^(XKBMODEL|XKBOPTIONS|BACKSPACE)=' "$kb" 2>/dev/null ||
                echo 'XKBMODEL="pc105"'
            printf 'XKBLAYOUT="%s"\n' "$layout"
            printf 'XKBVARIANT="%s"\n' "$variant"
        } > "$kb.new" && mv "$kb.new" "$kb"
        record keyboard "$layout${variant:+:$variant}"
        say "keyboard: $layout${variant:+ ($variant)}"
    else
        say "keyboard '$v' does not map to a layout this system has; left as it was"
    fi
fi

# ── the PC's clock ─────────────────────────────────────────────────
#
# THIS INSTALL LIVES BESIDE WINDOWS, and Windows keeps the PC's hardware
# clock in LOCAL time. Linux reads it as UTC unless told otherwise, so
# every machine west of Greenwich started hours BEHIND: apt refused every
# Release file as "not valid yet", unattended-upgrades never installed a
# fix, and every certificate looked wrong until something corrected the
# clock. So, once the zone is known:
#
#   /etc/adjtime says LOCAL. From the next start systemd (as on any
#     dual-boot PC) applies the zone's offset to what the clock says, so
#     the time is right before the network is up.
#   And right now, once, if the clock still shows the hardware's value
#     (it is hours out, and not years: a dead battery is not "a zone"),
#     the time is set from the hardware clock read as local time.
#
# chrony (rootfs/etc/chrony/chrony.conf) then corrects any remaining
# error from the network, and never writes the hardware clock back, so
# Windows' clock stays as it was. `rtc=utc` in choices.conf (for a
# Windows that was told to keep UTC) leaves all of this alone.
clock_local() { # zone
    if [ "$(get rtc)" = utc ]; then
        say "clock: kept in UTC, as Windows says; left as it is"
        return
    fi
    printf '0.0 0 0.0\n0\nLOCAL\n' > "$ROOT/etc/adjtime" || return
    say "clock: the PC's clock is read as local time, as Windows keeps it"
    [ -z "$ROOT" ] || return             # tests: files only
    d=$(cat /sys/class/rtc/rtc0/date 2>/dev/null); t=$(cat /sys/class/rtc/rtc0/time 2>/dev/null)
    [ -n "$d" ] && [ -n "$t" ] || return
    rl=$(TZ="$1" date -d "$d $t" +%s 2>/dev/null) || return
    delta=$((rl - $(date +%s)))
    a=${delta#-}
    if [ "$a" -ge 600 ] && [ "$a" -le 54000 ]; then
        date -u -s "@$rl" >/dev/null 2>&1 && say "clock: set from the PC's clock, $delta seconds"
    fi
}

# ── time zone ──────────────────────────────────────────────────────
#
# An IANA name, or a Windows one mapped through the same CLDR table
# ferry-settings uses. Either way it has to exist under zoneinfo.
v=$(get timezone)
tz=""
case "$v" in
    iana:*)    tz=${v#iana:} ;;
    windows:*) w=${v#windows:}
               tz=$(awk -F'\t' -v k="$w" '/^#/ || NF < 2 { next } $1 == k { print $2; exit }' \
                    "$SHARE/windowsZones.tab" 2>/dev/null) ;;
esac
if [ -n "$v" ]; then
    if [ -n "$tz" ] && matches "$tz" '[A-Za-z0-9_+-]+(/[A-Za-z0-9_+-]+){0,2}' &&
       [ -f "$ROOT/usr/share/zoneinfo/$tz" ]; then
        ln -sf "/usr/share/zoneinfo/$tz" "$ROOT/etc/localtime"
        printf '%s\n' "$tz" > "$ROOT/etc/timezone"
        record timezone "$tz"
        say "time zone: $tz"
        clock_local "$tz"
    else
        say "time zone '$v' is not one this system has; left as it was"
    fi
fi

# ── look, and way of working ───────────────────────────────────────
#
# By name, and only a name this system ships: /etc/auros/theme is read
# by the next step of first boot (aurora set), and active.shell is the
# one symlink build/forge makes to decide how the whole desktop behaves.
# AND THE PROGRAMS' OWN LOOK FOLLOWS IT. build/forge writes
# gtk-application-prefer-dark-theme into every account's GTK settings
# from the theme the IMAGE was built in -- Nocturne, which is dark --
# and every card in the installer picks Sandstone, which is light. So
# the desktop came up light and Firefox, the store and her files came
# up dark: two looks on one screen, the program she opens most in the
# one she did not choose. The colours themselves follow already (aurora
# renders /etc/auros/gtk/gtk.css, which those settings files import);
# this is the light-or-dark switch that sits beside them.
gtk_follow() { # theme
    _variant=$(sed -n 's/^theme_variant="\{0,1\}\([a-z]*\)"\{0,1\}.*/\1/p' \
               "$ROOT/usr/share/auros/themes/$1.theme" 2>/dev/null | head -n 1)
    case "$_variant" in
        light) _dark=0 ;;
        dark)  _dark=1 ;;
        *)     return 0 ;;           # the theme does not say; leave it
    esac
    for _ini in "$ROOT"/etc/skel/.config/gtk-[34].0/settings.ini \
                "$ROOT"/home/*/.config/gtk-[34].0/settings.ini; do
        # A regular file, never a link: this runs as root. sed -i keeps
        # the file's owner, so it stays hers.
        [ -f "$_ini" ] && [ ! -L "$_ini" ] || continue
        sed -i "s/^gtk-application-prefer-dark-theme=.*/gtk-application-prefer-dark-theme=$_dark/" \
            "$_ini" 2>/dev/null || say "could not set light/dark in $_ini"
    done
}

v=$(get theme)
if [ -n "$v" ]; then
    if matches "$v" '[a-z0-9-]{1,32}' && [ -f "$ROOT/usr/share/auros/themes/$v.theme" ]; then
        printf '%s\n' "$v" > "$ROOT/etc/auros/theme"
        gtk_follow "$v"
        record theme "$v"
        say "theme: $v"
    else
        say "theme '$v' is not one this system has; left as it was"
    fi
fi
v=$(get shell)
if [ -n "$v" ]; then
    if matches "$v" '[a-z0-9-]{1,32}' && [ -f "$ROOT/usr/share/auros/shells/$v.shell" ]; then
        mkdir -p "$ROOT/etc/auros/shell"
        ln -sf "/usr/share/auros/shells/$v.shell" "$ROOT/etc/auros/shell/active.shell"
        record shell "$v"
        say "desktop: $v"
    else
        say "desktop '$v' is not one this system has; left as it was"
    fi
fi

mv "$OUT.new" "$OUT"
exit 0
