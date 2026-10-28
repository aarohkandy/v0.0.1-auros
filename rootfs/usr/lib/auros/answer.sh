#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  answer.sh — the things the desktop may ask for as root (the five words,
#  and import:NAME, and stick:/dev/NAME)
#
#  Started by auros-answer.path when a file appears in /run/auros,
#  which is aurshell.service's own RuntimeDirectory: mode 0700, owned
#  by the person using the machine.
#
#  ROOT DOES NOT OPEN A PATH SHE OWNS. That is the whole shape of this
#  script and it is the second attempt at it. The first one read the
#  request with `head -c 64 "$REQ"` and wrote its answer, its log and
#  its report through `$RUN/...` -- as root, through names she
#  controls, with nothing to stop any of them being a symbolic link. An
#  adversarial review reproduced truncate-anything, append-anything and
#  chmod-anything from one request, and a read oracle for root-only
#  files from another. That is a local root escalation, on every built
#  image, reachable by anything running as her.
#
#  So:
#
#    reading  the request is taken by `aurfirst request`, in C, with
#             O_NOFOLLOW and an fstat on the descriptor it reads from
#             -- not a stat on the name, which is a race she wins. See
#             src/aurfirst/request.c.
#    writing  everything goes into /run/auros-answer, which is THIS
#             service's own RuntimeDirectory: root-owned, mode 0755,
#             world-readable. The desktop reads from there and cannot
#             put anything in it.
#
#  The word is still not a command. It is matched against a fixed list
#  of four and anything else is written down as unknown.
# ═══════════════════════════════════════════════════════════════════
set -u
# Overridable for tools/aurfirsttest.sh and for nothing else; the unit
# sets neither.
OUT=${AUROS_ANSWER_OUT:-/run/auros-answer}
STATE=${AUROS_STATE:-/var/lib/auros}
BIN=${AUROS_BIN:-/usr/sbin}
LOG=${AUROS_LOG:-/var/log/auros-answer.log}
RES="$OUT/result"
GRUBENV=${AUROS_GRUBENV:-/boot/grub/grubenv}
REBOOT=${AUROS_REBOOT:-systemctl --no-block reboot}
# The hardware clock, as UTC seconds: the reading the restore compares
# its own with (src/aurstage/main.c, restore_must_be_fresh).
RTC=${AUROS_RTC:-/sys/class/rtc/rtc0/since_epoch}
# The one entry "Put Windows back" restarts into: the "Yes" inside the
# submenu build/mkimage writes, by id.
PUTBACK_ENTRY='put-windows-back>put-windows-back-yes'
exec >>"$LOG" 2>&1
echo "=== $(date -Is) ==="

mkdir -p "$OUT" 2>/dev/null
chmod 0755 "$OUT" 2>/dev/null

# ONE WORD, TAKEN SAFELY AND CONSUMED. Exit 1 means there was nothing
# there -- a spurious edge, or a request another run has already dealt
# with -- and there is nothing to answer.
word=$("$BIN/aurfirst" request 2>/dev/null)
rc=$?
[ "$rc" = 1 ] && { echo "nothing to answer"; exit 0; }

# "import:NAME" is "import, and the files are NAME's": the answer to
# "whose files?" (result=choose below). NAME is only ever an argument to
# `ferry run --user`, which accepts nothing but a real Windows account.
iuser=
case "$word" in
  import:*)
    iuser=${word#import:}
    word=import
    case "$iuser" in ''|-*) iuser=- ;; esac
    [ "${#iuser}" -le 64 ] || iuser=-
    ;;
esac

# "stick:/dev/NAME" is "save her files on this USB stick" (the answer to
# "Save my files to a USB stick first"). aurfirst has already kept out
# everything but the plain name of a block device; below, the name must also
# be one `ferry stick list` offers right now, so root never writes to a disk
# the list would not have shown her.
sdev=
case "$word" in
  stick:*) sdev=${word#stick:}; word=stick ;;
esac

say() { printf '%s\n' "$*" >> "$RES.new"; }

# GRUB'S ENVIRONMENT BLOCK, WRITTEN WHOLE. Exactly 1024 bytes, which
# is the format grub's load_env and save_env read and rewrite in place;
# written beside and moved over, so a power cut leaves the old block or
# the new one and never half of each. Written here rather than with
# grub-editenv so that the one thing this depends on is sh.
grubenv_put() { # [key=value ...]
    t="$GRUBENV.new"
    { printf '# GRUB Environment Block\n'
      for kv in "$@"; do printf '%s\n' "$kv"; done
    } > "$t" || return 1
    have=$(wc -c < "$t")
    [ "$have" -le 1024 ] || { rm -f "$t"; return 1; }
    head -c $((1024 - have)) /dev/zero | tr '\0' '#' >> "$t" || return 1
    sync "$t" 2>/dev/null
    mv -f "$t" "$GRUBENV"
}

# IS THE WAY BACK STILL ON THIS COMPUTER. The restore starts from the
# staging kernel the installer left in \EFI\AurOS on the machine's own
# EFI partition. Looked for read-only, on every EFI partition, and
# unmounted again; AurOS never writes there. AUROS_ESP_ROOTS (tests
# only) names directories to look in instead of mounting anything.
staging_present() {
    if [ -n "${AUROS_ESP_ROOTS:-}" ]; then
        for d in $AUROS_ESP_ROOTS; do
            [ -f "$d/EFI/AurOS/staging.efi" ] && [ -f "$d/EFI/AurOS/staging.img" ] && return 0
        done
        return 1
    fi
    found=1
    for dev in $(lsblk -rno PATH,PARTTYPE 2>/dev/null |
                 awk 'tolower($2)=="c12a7328-f81f-11d2-ba4b-00a0c93ec93b"{print $1}'); do
        # /tmp, which this unit has privately (PrivateTmp=yes): under
        # ProtectSystem=strict, /run outside our own directories is
        # read-only, and a mount point there could not be made -- so
        # every look found nothing and "Put Windows back" said the files
        # were gone on a machine that had them.
        m=$(mktemp -d "${TMPDIR:-/tmp}/auros-esp.XXXXXX") || continue
        if mount -o ro "$dev" "$m" 2>/dev/null; then
            [ -f "$m/EFI/AurOS/staging.efi" ] && [ -f "$m/EFI/AurOS/staging.img" ] && found=0
            umount "$m" 2>/dev/null
        fi
        rmdir "$m" 2>/dev/null
        [ "$found" = 0 ] && return 0
    done
    return 1
}
# WHEN SHE ASKED, read as late as possible (see putback).
read_asked() {
    asked=$(cat "$RTC" 2>/dev/null)
    case "$asked" in ''|*[!0-9]*) asked= ;; esac
    [ -n "$asked" ]
}

# SAVE HER FILES IN WINDOWS BEFORE WINDOWS IS PUT BACK. Returns 0 only
# when they are saved (or there is no Ferry to do it: an image without
# one cannot have brought anything across either). Exit codes are
# `ferry back`'s: 2 some could not be saved, 3 the Windows drive was not
# shut down properly, 4 not enough room, 5 the drive could not be opened.
HANDOVER=${AUROS_HANDOVER:-/usr/lib/auros/handover.sh}
INSTALL_CHOICES=${AUROS_INSTALLER_CHOICES:-/etc/auros/installer-choices.conf}
handover_rec=
# HER FILES GO TO A HANDOVER VOLUME, NOT INTO WINDOWS, when this install was made by the fast
# path (choices.sh records via=gap): only that install registered the Windows task that takes
# them out of it and into her profile. Windows' drive is not touched at all -- on a BitLocker PC
# AurOS could not open it. (docs/AURBRIDGE.md, "Put Windows back with her files".)
handover_wanted() {
    [ -x "$HANDOVER" ] && grep -qx 'via=gap' "$INSTALL_CHOICES" 2>/dev/null
}
back_files() {
    [ -x "${AUROS_FERRY:-/usr/bin/ferry}" ] || return 0
    : > "$OUT/putback.log"
    chmod 0644 "$OUT/putback.log" 2>/dev/null
    HV=
    handover_rec=
    if handover_wanted; then
        need=$("${AUROS_FERRY:-/usr/bin/ferry}" handover --need 2>>"$OUT/putback.log")
        case "$need" in ''|*[!0-9]*) need=-1 ;; esac
        if [ "$need" = -1 ]; then
            say "result=failed"
            say "note=AurOS could not work out how much room your files need, so Windows was not put back. Nothing of yours was deleted"
            return 1
        fi
        if [ "$need" -gt 0 ]; then
            HV=$("$HANDOVER" prepare 2>>"$OUT/putback.log"); hrc=$?
            if [ "$hrc" != 0 ]; then
                say "result=failed"
                say "code=$hrc"
                case "$hrc" in
                  3|4) say "why=toomany"; say "note=your files are more than AurOS can set aside to hand to Windows, so Windows was not put back. Nothing of yours was deleted. Save them to a USB stick first, then try again" ;;
                  5)   say "why=locked"; say "note=AurOS cannot hand your files to Windows on this computer (its drive is encrypted), so Windows was not put back. Nothing of yours was deleted. Save them to a USB stick first, then try again" ;;
                  *)   say "note=AurOS could not set aside room for your files, so Windows was not put back. Nothing of yours was deleted" ;;
                esac
                return 1
            fi
            FERRY_HANDOVER_DEV=$HV
            export FERRY_HANDOVER_DEV
        fi
    fi
    # The same long limit as the import, for the same reason (it is not
    # the unit's any more); a copy that is stopped is picked up again.
    timeout "${AUROS_IMPORT_LIMIT:-43200}" "${AUROS_FERRY:-/usr/bin/ferry}" back --plain >>"$OUT/putback.log" 2>&1
    brc=$?
    # Saved: say how many, so the screen can tell her what was and was not
    # saved rather than "your files are safe".
    bfiles=$(sed -n 's/^files=//p' "$OUT/ferry.done" 2>/dev/null | head -1)
    if [ -n "$HV" ]; then
        unset FERRY_HANDOVER_DEV
        if [ "$brc" = 0 ]; then
            # The volume is finished and unmounted: detach it, and say exactly where on the
            # disk it lies, proved by reading the disk.
            if ! handover_rec=$("$HANDOVER" finish 2>>"$OUT/putback.log"); then
                "$HANDOVER" cancel
                say "result=failed"
                say "note=AurOS could not leave your files where Windows can find them, so Windows was not put back. Nothing of yours was deleted. Save them to a USB stick first, then try again"
                return 1
            fi
        else
            "$HANDOVER" cancel
        fi
    fi
    [ "$brc" = 0 ] && return 0
    say "result=failed"
    say "code=$brc"
    case "$brc" in
      2) say "why=some"; say "note=some of your files could not be saved in Windows first, so Windows was not put back. The ones that could be saved are in Windows, in the folder From AurOS; nothing of yours was deleted" ;;
      3) say "why=dirty"; say "note=Windows was not shut down properly, so your files could not be saved in it first; start Windows, shut it down fully, then try again. Nothing of yours was deleted" ;;
      4) say "why=noroom"; say "note=there is not enough room in Windows to save your files first, so Windows was not put back. Nothing of yours was deleted" ;;
      5) say "why=locked"; say "note=your files could not be saved in Windows first (its drive could not be opened, for example because it is locked), so Windows was not put back. Nothing of yours was deleted" ;;
      7) say "why=nodrive"; say "note=your files could not be saved in Windows first, because no Windows drive was found on this computer, so Windows was not put back. Nothing of yours was deleted" ;;
      *) say "note=your files could not be saved in Windows first, so Windows was not put back. Nothing of yours was deleted" ;;
    esac
    return 1
}

: > "$RES.new"
say "request=$word"

case "$word" in
  confirm)
    if "$BIN/aurfirst" confirm 2>&1; then
        say "result=ok"
        say "note=AurOS is now what this computer starts"
    else
        say "result=failed"
        say "note=this computer would not let AurOS change what it starts"
    fi
    ;;
  decline)
    if "$BIN/aurfirst" decline 2>&1; then
        say "result=ok"
        say "note=the next start will reach Windows"
    else
        say "result=failed"
        say "note=this computer would not let AurOS change its start-up setting"
    fi
    ;;
  import)
    # THE GATE IS ASKED, NOT ASSUMED. Copying somebody's documents is
    # the first thing this product does that restarting cannot undo,
    # so it waits until AurOS has been confirmed -- and that decision
    # lives in aurfirst, in one place, rather than being re-derived
    # here from a stamp file this script would have to know about.
    if ! "$BIN/aurfirst" ferry >/dev/null 2>&1; then
        say "result=refused"
        say "note=AurOS has not been confirmed yet"
    elif [ "$iuser" = - ]; then
        say "result=failed"
        say "note=that is not the name of a Windows account"
    elif [ ! -x "${AUROS_FERRY:-/usr/bin/ferry}" ]; then
        say "result=failed"
        say "note=this image has no way to import from Windows"
    else
        FERRY=${AUROS_FERRY:-/usr/bin/ferry}
        : > "$OUT/import.log"
        chmod 0644 "$OUT/import.log" 2>/dev/null
        # --plain: import.log, whose newest line is what she reads while
        # it runs, gets only plain sentences; Ferry's own chatter goes to
        # /var/lib/ferry/ferry.log.
        #
        # NO ONE-HOUR LIMIT. A big photo library on a ten-year-old disk
        # takes hours, and the unit used to be killed at TimeoutStartSec=
        # 3600 in the middle of it (the unit has none now). A cap remains
        # here, a long one, only so that a drive that has stopped
        # answering cannot hold every later request for ever. A run that
        # is stopped -- by this, by power, by a restart -- is picked up by
        # the next one without copying anything twice (ferry-copy).
        if [ -n "$iuser" ]; then
            timeout "${AUROS_IMPORT_LIMIT:-43200}" "$FERRY" run --auto --plain --user "$iuser" >>"$OUT/import.log" 2>&1
        else
            timeout "${AUROS_IMPORT_LIMIT:-43200}" "$FERRY" run --auto --plain >>"$OUT/import.log" 2>&1
        fi
        # CAPTURED BEFORE ANYTHING ELSE RUNS. `$?` inside an else
        # branch is the status of the last command executed, which
        # after one line of reporting is the reporting.
        frc=$?
        mkdir -p "$STATE"
        "$FERRY" report > "$STATE/import-report.txt" 2>/dev/null
        cp "$STATE/import-report.txt" "$OUT/import-report.txt" 2>/dev/null
        chmod 0644 "$OUT/import-report.txt" 2>/dev/null
        # How many files Ferry says it has brought across, so that "ok"
        # can never mean "ok, and nothing came".
        nfiles=$(sed -n 's/^files=//p' "$OUT/ferry.done" 2>/dev/null | head -1)
        # FERRY'S 3 IS NOT A PARTIAL IMPORT. ferry-common.sh's refuse()
        # exits 3 before any stage starts -- a hibernated or dirty
        # volume, which Fast Startup makes the COMMON case -- and
        # nothing was read at all. Reporting that as "partial" made the
        # desktop say "Your files are here" on the most likely import
        # failure there is.
        if [ "$frc" = 0 ] && [ "${nfiles:-x}" = 0 ]; then
            # Everything it was asked to bring came -- and there was
            # nothing. Not "your files are here".
            say "result=empty"
            say "files=0"
            say "note=there were no files on the Windows side to bring across"
        elif [ "$frc" = 0 ]; then
            say "result=ok"
            [ -z "$nfiles" ] || say "files=$nfiles"
        elif [ "$frc" = 3 ]; then
            say "result=refused"
            say "code=3"
            say "note=the Windows drive was not shut down properly"
        elif [ "$frc" = 6 ]; then
            # Several Windows accounts and no way to tell whose files are
            # hers. Ferry does not guess between people. The names are
            # for the desktop to ask with; the answer comes back as
            # "import:NAME".
            say "result=choose"
            say "code=6"
            say "users=$(tr '\n' ',' < "${FERRY_STATE:-/var/lib/ferry}/users" 2>/dev/null | sed 's/,$//')"
            say "note=more than one person uses Windows on this computer, so AurOS needs to be told whose files to bring"
        elif [ "$frc" = 2 ]; then
            # Ferry's 2: it ran, and something of hers did not come (an
            # online-only OneDrive file, a file Windows locked). Its 1
            # is "nothing could be brought", which "Your files are
            # here" must never answer.
            say "result=partial"
            say "code=2"
            [ -z "$nfiles" ] || say "files=$nfiles"
        elif [ "$frc" = 4 ]; then
            say "result=failed"
            say "code=4"
            say "note=there is not enough room on this computer for your files; nothing was copied"
        elif [ "$frc" = 124 ] || [ "$frc" = 137 ]; then
            say "result=failed"
            say "code=$frc"
            say "note=bringing your files across took far too long and was stopped; press it again and it carries on where it stopped"
        else
            say "result=failed"
            say "code=$frc"
            say "note=your files could not be brought across from Windows"
        fi
    fi
    ;;
  stick)
    # SAVE HER FILES ON A USB STICK, her choice of stick. The same copier and
    # read-back as the way back (ferry stick), into a folder she can open on
    # any computer; nothing on the stick is deleted, nothing here is armed,
    # and Put Windows back is not asked. Its plain progress lines are in
    # $OUT/stick.log, the newest is what the screen shows, and $OUT/ferry.done
    # says how it ended (kind=stick).
    FERRY=${AUROS_FERRY:-/usr/bin/ferry}
    if [ ! -x "$FERRY" ]; then
        say "result=failed"
        say "note=this image has no way to save your files on a USB stick"
    elif [ -z "$sdev" ] || ! "$FERRY" stick list 2>/dev/null | cut -f1 | grep -qxF "$sdev"; then
        say "result=failed"
        say "why=nostick"
        say "note=that USB stick is not there any more"
    else
        : > "$OUT/stick.log"
        chmod 0644 "$OUT/stick.log" 2>/dev/null
        timeout "${AUROS_IMPORT_LIMIT:-43200}" "$FERRY" stick "$sdev" --plain >>"$OUT/stick.log" 2>&1
        src=$?
        nfiles=$(sed -n 's/^files=//p' "$OUT/ferry.done" 2>/dev/null | head -1)
        case "$src" in
          0) if [ "${nfiles:-x}" = 0 ]; then
                 say "result=empty"; say "files=0"
                 say "note=there were no files of yours in AurOS to save"
             else
                 say "result=ok"; [ -z "$nfiles" ] || say "files=$nfiles"
             fi ;;
          2) say "result=partial"; say "code=2"; [ -z "$nfiles" ] || say "files=$nfiles" ;;
          4) say "result=failed"; say "code=4"; say "why=noroom"
             say "note=there is not enough room on the USB stick for your files; nothing was copied" ;;
          5) say "result=failed"; say "code=5"; say "why=unopened"
             say "note=AurOS could not open the USB stick to save your files" ;;
          124|137) say "result=failed"; say "code=$src"
             say "note=saving your files took far too long and was stopped; press it again and it carries on where it stopped" ;;
          *) say "result=failed"; say "code=$src"
             say "note=your files could not be saved on the USB stick" ;;
        esac
    fi
    ;;
  putback)
    # PUT WINDOWS BACK. Not done here: a restore rewrites the partition
    # table of the disk it runs from, which cannot be done safely from
    # a running AurOS (docs/AURBRIDGE.md, "Putting it back"). This
    # arranges the next start -- AurOS's menu, told to take the restore
    # entry once, and the firmware told to start that menu once -- and
    # restarts. Each step that fails takes the ones before it back: a
    # next_entry left behind would remove AurOS on some later start
    # nobody asked for.
    # WHEN SHE ASKED, written beside the request. The restore runs only
    # if it starts within the hour (src/aurstage/main.c): a request the
    # firmware did not act on -- it started Windows instead -- must not
    # remove AurOS at whatever later start next reaches this menu.
    # THE FILES SHE SAVED IN AUROS GO INTO WINDOWS FIRST. Putting Windows
    # back removes AurOS and everything she kept in it; `ferry back`
    # copies her Documents, Pictures, Desktop, Downloads, Music and
    # Videos into her Windows user folder, under "From AurOS", and
    # nothing is armed unless that finished. Its plain progress lines are
    # in $OUT/putback.log (the newest is what the screen shows) and, when
    # it ends, $OUT/ferry.done says how (src/ferry/ferry-common.sh).
    # AFTER THE COPY, because it can take a long time and "within the
    # hour" below is counted from the moment the restore is armed.
    if ! staging_present; then
        say "result=failed"
        say "note=the files that put Windows back are not on this computer any more"
    elif ! back_files; then
        :   # back_files has said why
    elif ! read_asked; then
        say "result=failed"
        say "note=AurOS could not read this computer's clock"
    elif ! { if [ -n "$handover_rec" ]; then
                 grubenv_put "next_entry=$PUTBACK_ENTRY" "auros_asked=$asked" "auros_handover=$handover_rec"
             else
                 grubenv_put "next_entry=$PUTBACK_ENTRY" "auros_asked=$asked"
             fi; }; then
        [ -n "$handover_rec" ] && "$HANDOVER" cancel
        say "result=failed"
        say "note=AurOS could not set its start-up menu"
    elif ! "$BIN/aurfirst" putback 2>&1; then
        [ -n "$handover_rec" ] && "$HANDOVER" cancel
        say "result=failed"
        # THE TAKE-BACK IS CHECKED. A next_entry left behind would run
        # the restore at some later start, so if it cannot be cleared the
        # answer has to say so -- "failed" alone reads as "nothing will
        # happen", which would then not be true.
        if grubenv_put; then
            say "note=this computer would not let AurOS arrange the restart"
        else
            say "note=the restart could not be arranged, and AurOS's start-up menu may still run the restore next time; choose No in it"
        fi
    else
        say "result=ok"
        # How many files are in Windows now (0: she had none to save).
        say "saved=${bfiles:-0}"
        say "note=restarting to put Windows back"
        putback_restart=1
    fi
    ;;
  windows)
    # START WINDOWS, THIS ONCE. The firmware is told to start Windows Boot
    # Manager next (BootNext) and the computer restarts; BootOrder and
    # her answer are not touched, so the start after that is AurOS again.
    # Windows is started by the firmware itself, not by this menu or by
    # grub, so BitLocker sees the start it sealed its key to
    # (rootfs/usr/lib/auros/start-windows says why). Untimed: it is a
    # button, not a firmware menu with a clock.
    if "$BIN/aurfirst" windows 2>&1; then
        say "result=ok"
        say "note=restarting to start Windows"
        putback_restart=1
    else
        say "result=failed"
        say "note=this computer would not let AurOS start Windows this once"
    fi
    ;;
  *)
    say "result=refused"
    say "note=unknown request"
    ;;
esac

# WRITTEN WHOLE, THEN MOVED. The desktop polls this file; one that is
# half written is one it reads as an answer that is missing its result
# line, and then acts on. Both paths are inside a directory root owns,
# so neither can be a link to somewhere else.
chmod 0644 "$RES.new" 2>/dev/null
mv -f "$RES.new" "$RES"
echo "answered: $(cat "$RES")"
# After the answer is written, so the desktop can say what is about to
# happen before it does.
if [ "${putback_restart:-0}" = 1 ]; then
    sleep "${AUROS_REBOOT_DELAY:-3}"
    $REBOOT
fi
exit 0
