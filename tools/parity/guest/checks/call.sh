# (a) A video call works in the browser: a real camera device and microphone, a WebRTC
# connection, and the other side receives the picture and the sound.
. /usr/lib/auros-parity/lib.sh
serve || { result call fail "the test web server did not start"; exit 0; }

# The camera is /dev/video0 (the kernel's own test camera, vivid: see root.sh).
# The microphone is a PipeWire source fed with a 440 Hz tone, so "sound arrives"
# means something: a virtual sink + source pair, and a loop playing into the sink.
python3 - <<'PY'
import wave, struct, math
w = wave.open('/var/lib/auros-parity/out/tone.wav', 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(48000)
w.writeframes(b''.join(struct.pack('<h', int(9000 * math.sin(2 * math.pi * 440 * i / 48000))) for i in range(48000)))
w.close()
PY
pw-loopback -n parity-mic -m '[ FL FR ]' \
    --capture-props='media.class=Audio/Sink node.name=parity-mic-in node.description=Parity-Microphone-In' \
    --playback-props='media.class=Audio/Source node.name=parity-mic node.description=Parity-Microphone' >/dev/null 2>&1 &
LB=$!
wait_for 15 sh -c 'pw-cli ls Node 2>/dev/null | grep -q parity-mic'
( while :; do pw-play --target parity-mic-in "$OUT/tone.wav" >/dev/null 2>&1 || sleep 1; done ) &
TONE=$!
sleep 1
wpctl set-default "$(wpctl status | sed -n 's/.*[^0-9]\([0-9][0-9]*\)\. Parity-Microphone .*/\1/p' | head -1)" 2>/dev/null
note "audio sources: $(wpctl status | sed -n '/Sources:/,/Filters:/p' | tr '\n' '|' | cut -c1-300)"

clear_report call
fx_start http://127.0.0.1:8123/call.html
sleep 25
snap call-1
sleep 2
if wait_report call 120; then
    sleep 3; snap call
    act hue call call.visible 260 130 1020 690 3
    ok=$(jget "$OUT/call.json" ok)
    note "call report: $(cat "$OUT/call.json" | cut -c1-400)"
    if [ "$ok" = True ]; then
        result call.webrtc pass "the other side got $(jget "$OUT/call.json" remote_size) video ($(jget "$OUT/call.json" frames_decoded) frames, $(jget "$OUT/call.json" colours) colours) and sound; camera: $(jget "$OUT/call.json" video_label)"
    else
        result call.webrtc fail "$(cat "$OUT/call.json" | cut -c1-300)"
    fi
else
    result call.webrtc fail "the page never reported; last stage: $(cat "$OUT/call-stage.json" 2>/dev/null || echo none); $(tail -c 300 /var/lib/auros-parity/firefox.log | tr '\n' ' ')"
fi
fx_stop; kill "$TONE" "$LB" 2>/dev/null; pkill pw-play; unserve
