#!/usr/bin/env bash
# End-to-end GUI test: drives the real game with injected X11 keyboard/mouse events (XTest)
# on a virtual display and asserts on observable results (save files, process alive).
# Usage: tests/e2e_gui.sh <build-dir>   (Linux; needs Xvfb, libX11, libXtst)
set -euo pipefail
BUILD=$(cd "${1:-build}" && pwd)
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
DISP=:$((90 + RANDOM % 9))
XTST=$( (ls /usr/lib/*/libXtst.so* /usr/lib/libXtst.so* 2>/dev/null || true) | head -1)
cc -O1 -o "$WORK/xinj" "$ROOT/tools/xinj.c" -lX11 "$XTST"
Xvfb "$DISP" -screen 0 1280x720x24 >/dev/null 2>&1 & XPID=$!
GPID=
cleanup() { [ -n "$GPID" ] && kill "$GPID" 2>/dev/null || true; kill "$XPID" 2>/dev/null || true; }
trap cleanup EXIT
sleep 1
inj() { DISPLAY=$DISP "$WORK/xinj"; }
start_game() { # extra env as args
  (cd "$BUILD" && env DISPLAY=$DISP CARO_DATA_DIR="$WORK/data" "$@" ./caro >"$WORK/game.log" 2>&1) & GPID=$!
  sleep $(( 2 * ${XINJ_SCALE:-1} ))
}
fail() { echo "E2E FAIL: $*"; ls -la "$WORK" "$WORK/data" 2>/dev/null; exit 1; }

echo "== 1. Home -> New Game (PvP 3x3, keyboard) -> Profile -> Start -> X wins by keyboard"
start_game CARO_AFK_IDLE=600
inj <<'K'
key Return
sleep 700
key Right
key Return
key Down
key Return
sleep 200
click 895 620
sleep 800
click 756 641
sleep 900
move 1200 100
key space
sleep 200
key Up
key Left
key Return
sleep 200
key w
key space
sleep 200
key Down
key Down
key Return
sleep 200
key s
key s
key space
sleep 2000
sleep 10
K
kill -0 $GPID || fail "game exited during match"

echo "== 2. Rematch (Enter) -> Esc pause -> Save Game -> slot 3"
inj <<'K'
key Return
sleep 900
key space
sleep 300
key Escape
sleep 500
click 640 281
sleep 900
click 356 280
sleep 900
K
[ -f "$WORK/data/slot03.sav" ] || fail "manual save to slot 3 missing"
grep -q '^end=1' "$WORK/data/slot03.sav" || fail "slot 3 save incomplete"
# Step 1 must have ended in a win: only then does Enter start a rematch, leaving exactly the
# one move played in step 2 (otherwise the first match would have continued with 6+ moves).
grep -q '^moves=1$' "$WORK/data/slot03.sav" || fail "step 1 did not finish with a win (rematch not started)"

echo "== 3. Leave -> Home -> Load Game -> slot 3"
inj <<'K'
click 648 576
sleep 600
click 766 446
sleep 900
click 640 459
sleep 900
click 356 280
sleep 900
K
kill -0 $GPID || fail "game exited after load"
kill $GPID; GPID=; sleep 0.5

echo "== 4. AFK: prompt then autosave + Home"
rm -f "$WORK/data/slot0"*.sav
start_game CARO_AFK_IDLE=3 CARO_AFK_GRACE=3
inj <<'K'
key Return
sleep 600
click 895 620
sleep 600
click 756 641
sleep 800
move 1200 100
key space
sleep 8000
K
ls "$WORK/data"/slot*.sav >/dev/null 2>&1 || fail "AFK autosave missing"
kill -0 $GPID || fail "game exited after AFK"
echo "E2E GUI: all checks passed"
