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
TRACE="$WORK/trace.txt"
start_game() { # extra env as args
  : > "$TRACE"
  (cd "$BUILD" && env DISPLAY=$DISP CARO_DATA_DIR="$WORK/data" CARO_TRACE="$TRACE" "$@" ./caro >"$WORK/game.log" 2>&1) & GPID=$!
  MARK=0; waitfor "scene 0" "game window up"
  sleep $(( 1 * ${XINJ_SCALE:-1} ))   # let the window map and take keyboard focus
}
fail() { echo "E2E FAIL: $*"; echo "--- trace"; cat "$TRACE"; ls -la "$WORK/data" 2>/dev/null; exit 1; }
# waitfor <exact trace line> <what>: the line must appear after the last mark (up to ~15 s)
MARK=0
mark() { MARK=$(wc -l < "$TRACE"); }
waitfor() {
  for _ in $(seq 1 60); do
    if tail -n +$((MARK + 1)) "$TRACE" | grep -qx "$1"; then
      echo "ok: $2"
      # a new screen animates its widgets in (slide/fade); give it time before the next input
      case "$1" in scene*|overlay*) sleep "$(awk "BEGIN{print 0.6*${XINJ_SCALE:-1}}")";; esac
      return 0
    fi
    sleep 0.25
  done
  fail "$2 (waited for '$1')"
}

echo "== 1. Home -> New Game (PvP 3x3, keyboard) -> Profile -> Start -> X wins by keyboard"
start_game CARO_AFK_IDLE=600
mark; echo "key Return" | inj; waitfor "scene 1" "Enter on Play opens New Game"
mark
inj <<'K'
key Right
key Return
key Down
key Return
sleep 300
click 895 620
K
waitfor "scene 2" "Next opens the profile screen"
mark; echo "click 756 641" | inj; waitfor "scene 5" "Start begins the match"
mark
inj <<'K'
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
K
waitfor "overlay 6" "X won by keyboard, game-over popup"
kill -0 $GPID || fail "game exited during match"

echo "== 2. Rematch (Enter) -> Esc pause -> Save Game -> slot 3"
mark; echo "key Return" | inj; waitfor "moves 0" "Enter on Rematch starts a new match"
sleep $(( 1 * ${XINJ_SCALE:-1} ))   # rematch fades game -> game (same scene id): let it finish
mark; echo "key space" | inj; waitfor "moves 1" "placed a stone"
mark; echo "key Escape" | inj; waitfor "overlay 1" "Esc opens Pause"
mark; echo "click 640 281" | inj; waitfor "scene 4" "Save Game opens the slots"
mark; echo "click 356 280" | inj; waitfor "scene 5" "saving returns to the match"
[ -f "$WORK/data/slot03.sav" ] || fail "manual save to slot 3 missing"
grep -q '^end=1' "$WORK/data/slot03.sav" || fail "slot 3 save incomplete"
# Step 1 must have ended in a win: only then does Enter start a rematch, leaving exactly the
# one move played in step 2 (otherwise the first match would have continued with 6+ moves).
grep -q '^moves=1$' "$WORK/data/slot03.sav" || fail "step 1 did not finish with a win (rematch not started)"

echo "== 3. Pause Home -> main screen (no confirm) -> Load Game -> slot 3"
mark; echo "click 648 576" | inj; waitfor "scene 0" "pause Home goes to the main screen"
mark; echo "click 640 459" | inj; waitfor "scene 4" "Load Game opens the slots"
mark; echo "click 356 280" | inj; waitfor "scene 5" "loading slot 3 resumes the match"
kill -0 $GPID || fail "game exited after load"
kill $GPID; GPID=; sleep 0.5

echo "== 4. AFK: prompt then autosave + Home"
rm -f "$WORK/data/slot0"*.sav
start_game CARO_AFK_IDLE=3 CARO_AFK_GRACE=3
mark; echo "key Return" | inj; waitfor "scene 1" "New Game"
mark; echo "click 895 620" | inj; waitfor "scene 2" "Profile"
mark; echo "click 756 641" | inj; waitfor "scene 5" "match"
mark; printf 'move 1200 100\nkey space\n' | inj; waitfor "overlay 5" "AFK prompt after 3 s idle"
waitfor "scene 0" "no answer: back to the main screen"
ls "$WORK/data"/slot*.sav >/dev/null 2>&1 || fail "AFK autosave missing"
kill -0 $GPID || fail "game exited after AFK"
kill $GPID; GPID=; sleep 0.5

echo "== 5. 2-player undo request (decline, accept) and surrender"
start_game CARO_AFK_IDLE=600
mark; echo "key Return" | inj; waitfor "scene 1" "New Game"
mark; printf 'key Right\nkey Return\nsleep 300\nclick 895 620\n' | inj; waitfor "scene 2" "2 players -> Profile"
mark; echo "click 756 641" | inj; waitfor "scene 5" "match"
mark; printf 'move 1200 100\nkey space\n' | inj; waitfor "moves 1" "player 1 (Space) placed"
mark; printf 'key Up\nkey Return\n' | inj; waitfor "moves 2" "player 2 (arrows + Enter) placed"
mark; echo "click 168 484" | inj; waitfor "overlay 2" "Undo asks the opponent for consent"
mark; echo "key Return" | inj; waitfor "overlay 0" "opponent declines (default focus)"
tail -n +$((MARK + 1)) "$TRACE" | grep -q "^moves 1$" && fail "declined undo still removed a move"
echo "ok: declined undo keeps the move"
mark; echo "click 168 484" | inj; waitfor "overlay 2" "Undo request again"
mark; printf 'key Right\nkey Return\n' | inj; waitfor "moves 1" "opponent accepts: the move is taken back"
mark; echo "click 168 556" | inj; waitfor "overlay 3" "Surrender asks for confirmation"
mark; printf 'key Right\nkey Return\n' | inj; waitfor "overlay 6" "Yes: surrender ends the match"

echo "== 6. Settings: language and music mute persist"
mark; echo "key Home" | inj; waitfor "scene 0" "Home"
mark; echo "click 640 535" | inj; waitfor "scene 3" "Settings"
echo "key Right" | inj; sleep 1
grep -qx "lang=1" "$WORK/data/settings.cfg" || fail "language switch not saved"
echo "ok: language switched to Vietnamese and saved"
printf 'key Down\nkey Down\nkey Return\n' | inj; sleep 1
grep -qx "bgm_muted=1" "$WORK/data/settings.cfg" || fail "music mute not saved"
echo "ok: music mute saved"
echo "E2E GUI: all checks passed"
