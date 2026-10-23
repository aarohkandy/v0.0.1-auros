# (b) A film plays: an H.264/AAC MP4, and a YouTube-like adaptive (DASH, Media Source) stream, in Firefox,
# with picture, sound and no dropped frames to speak of. Widevine/Netflix is v0.2 (WP-56).
. /usr/lib/auros-parity/lib.sh
serve || { result film fail "the test web server did not start"; exit 0; }

play() {   # NAME LABEL URL
    n=$1; label=$2; url=$3
    clear_report "$n"
    fx_start "http://127.0.0.1:8123/film.html?name=$n&$url"
    sleep 12
    rm -f "$OUT/$n.cpu"
    ( cpu_busy 6 > "$OUT/$n.cpu" ) &
    if wait_report "$n" 150; then
        note "$n: $(cut -c1-420 "$OUT/$n.json")"
        sleep 1; snap "$n"
        say "do pixels $n film.$label-visible 260 130 1020 690 12"; sleep 2
        wait_for 15 test -s "$OUT/$n.cpu"
        c=$(jget "$OUT/$n.json" current_time); f=$(jget "$OUT/$n.json" frames); d=$(jget "$OUT/$n.json" dropped)
        note "perf $n frames=$f dropped=$d cpu=$(cat "$OUT/$n.cpu" 2>/dev/null)% size=$(jget "$OUT/$n.json" size) streams=$(pw-cli ls Node 2>/dev/null | grep -ci firefox)"
        if [ "$(jget "$OUT/$n.json" ok)" = True ]; then
            result "film.$label" pass "played $(printf %.0f "$c") s, $f frames ($d dropped), sound out of the decoder, $(jget "$OUT/$n.json" size)"
        else
            result "film.$label" fail "$(cut -c1-300 "$OUT/$n.json")"
        fi
    else
        result "film.$label" fail "never finished; last stage: $(cat "$OUT/$n-stage.json" 2>/dev/null || echo none)"
    fi
    fx_stop; sleep 2
}
play film-h264 h264 "mode=file&src=/media/film-360.mp4"
play film-dash dash "mode=dash&src=/media/dash/manifest.mpd"
if opt perf; then play film-720 h264-720p "mode=file&src=/media/film-720.mp4"; fi
unserve
