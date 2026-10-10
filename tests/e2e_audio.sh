#!/usr/bin/env bash
# Audio behaviour test (Linux): captures what the GUI actually sends to the sound card, through
# an ALSA "file" device paced in real time, and checks:
#   music on -> steady non-silent output; music + effects muted -> silence.
# Usage: tests/e2e_audio.sh <build-dir>   (needs Xvfb, python3, ALSA)
set -euo pipefail
BUILD=$(cd "${1:-build}" && pwd)
WORK=$(mktemp -d)
cat > "$WORK/asound.conf" <<CONF
</usr/share/alsa/alsa.conf>
pcm.!default { type file slave.pcm "null" file "$WORK/out.raw" format "raw" }
CONF
cat > "$WORK/cap.py" <<'PY'
import array, math, sys, time
# raylib/miniaudio opens the device as 48 kHz stereo float32: read at real-time pace for 6 s
path, secs = sys.argv[1], float(sys.argv[2])
rate = 48000 * 2 * 4
f = open(path, 'rb'); buf = bytearray(); t0 = time.time()
while len(buf) < rate * secs:
    chunk = f.read(9600)
    if not chunk: break
    buf += chunk
    ahead = len(buf) / rate - (time.time() - t0)
    if ahead > 0: time.sleep(ahead)
a = array.array('f'); a.frombytes(bytes(buf[:len(buf)//4*4]))
tail = a[len(a)//2:]                     # skip the music fade-in
rms = math.sqrt(sum(x*x for x in tail) / max(1, len(tail)))
peak = max((abs(x) for x in tail), default=0.0)
print("%.6f %.6f %d" % (rms, peak, len(a)))
PY
run() { # run <settings bgm_muted> <sfx_muted> -> prints "rms peak samples"
  mkdir -p "$WORK/d$1$2"
  printf 'CARO-SETTINGS 1\nbgm_muted=%s\nsfx_muted=%s\n' "$1" "$2" > "$WORK/d$1$2/settings.cfg"
  rm -f "$WORK/out.raw"; mkfifo "$WORK/out.raw"
  python3 -I "$WORK/cap.py" "$WORK/out.raw" 6 > "$WORK/r$1$2.txt" &
  local cp=$!
  (cd "$BUILD" && ALSA_CONFIG_PATH="$WORK/asound.conf" CARO_DATA_DIR="$WORK/d$1$2" \
     timeout 20 xvfb-run -a -s "-screen 0 1280x720x24" ./caro --scene home --frames 600 \
     --shot "$WORK/x.png" >/dev/null 2>&1) || true
  wait $cp || true
  cat "$WORK/r$1$2.txt"
}
read -r rms peak n < <(run 0 0)
echo "music on:   rms=$rms peak=$peak samples=$n"
awk "BEGIN{exit !($n > 400000 && $rms > 0.005 && $peak < 0.99)}" || { echo "E2E AUDIO FAIL: expected steady, unclipped music"; exit 1; }
echo "ok: menu music plays (not silent, not clipping)"
read -r rms peak n < <(run 1 1)
echo "all muted:  rms=$rms peak=$peak samples=$n"
awk "BEGIN{exit !($n > 400000 && $peak < 0.0001)}" || { echo "E2E AUDIO FAIL: expected silence when muted"; exit 1; }
echo "ok: muting music and effects gives silence"
echo "E2E AUDIO: all checks passed"
