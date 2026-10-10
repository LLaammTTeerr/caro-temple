#!/usr/bin/env bash
# End-to-end test of the GUI version on macOS: real Quartz key events (tools/macinj.c posts them
# to the game process), assertions through the CARO_TRACE log. Usage: tests/e2e_gui_macos.sh <build-dir>
set -euo pipefail
BUILD=$(cd "${1:-build}" && pwd)
WORK=$(mktemp -d)
S=${XINJ_SCALE:-1}
TRACE="$WORK/trace.txt"
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cc -O1 -o "$WORK/macinj" "$ROOT/tools/macinj.c" -framework ApplicationServices
MARK=0
GPID=
cleanup() { [ -n "$GPID" ] && kill "$GPID" 2>/dev/null || true; }
trap cleanup EXIT
fail() { echo "E2E GUI (macOS) FAIL: $*"; echo "--- trace"; cat "$TRACE" 2>/dev/null; echo "--- log"; tail -20 "$WORK/game.log" 2>/dev/null; exit 1; }
mark() { MARK=$(wc -l < "$TRACE" | tr -d ' '); }
waitfor() {
  for _ in $(seq 1 80); do
    if tail -n +$((MARK + 1)) "$TRACE" 2>/dev/null | grep -qx "$1"; then
      echo "ok: $2"
      case "$1" in scene*|overlay*) sleep "$(awk "BEGIN{print 0.6*$S}")";; esac
      return 0
    fi
    sleep 0.25
  done
  fail "$2 (waited for '$1')"
}
# key codes: Return 36, Escape 53, Space 49, Left 123, Right 124, Down 125, Up 126, Home 115
front() { osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $GPID) to true" >/dev/null 2>&1 || true; }
key() { "$WORK/macinj" "$GPID" "$@"; sleep "$(awk "BEGIN{print 0.25*$S}")"; }
# press <keycode> <trace line> <what>: send one key and wait for its effect; if the key was
# dropped (no effect within ~2 s), send it again (up to 3 tries). Retrying is safe because the
# expected line only appears once the key has been handled.
press() {
  for try in 1 2 3; do
    mark; key "$1"
    for _ in $(seq 1 8); do
      if tail -n +$((MARK + 1)) "$TRACE" 2>/dev/null | grep -qx "$2"; then
        echo "ok: $3$([ $try -gt 1 ] && echo " (key resent $((try - 1))x)")"
        case "$2" in scene*|overlay*) sleep "$(awk "BEGIN{print 0.6*$S}")";; esac
        return 0
      fi
      sleep 0.25
    done
  done
  fail "$3 (no '$2' after 3 tries)"
}
start_game() {
  : > "$TRACE"
  (cd "$BUILD" && exec env CARO_DATA_DIR="$WORK/data" CARO_TRACE="$TRACE" CARO_AFK_IDLE=600 ./caro >"$WORK/game.log" 2>&1) & GPID=$!
  MARK=0; waitfor "scene 0" "window up (pid $GPID)"
  sleep "$(awk "BEGIN{print 1.5*$S}")"; front; sleep 1
}

echo "== menus and a match vs the computer"
start_game
press 36  "scene 1"   "Return on Play opens New Game"
press 53  "scene 0"   "Escape goes back to Home"
press 36  "scene 1"   "New Game again"
press 126 "focus 9"   "Up moves focus (wraps to Home button)"
press 126 "focus 8"   "Up moves focus to Next"
press 36  "scene 2"   "Return on Next opens Profile"
press 126 "focus 10"  "Up moves focus (wraps to Home button)"
press 126 "focus 9"   "Up moves focus to Start"
press 36  "scene 5"   "Return on Start begins the match"
press 49  "moves 1"   "Space placed a stone"
waitfor   "moves 2"   "computer replied"
# board cursor moves are not traced: each attempt moves the cursor one more step down-right
# (always onto a fresh diagonal cell) and presses Space until the third move lands
placed=0
for try in 1 2 3 4; do
  mark; key 124 125 49
  for _ in $(seq 1 8); do
    tail -n +$((MARK + 1)) "$TRACE" | grep -qx "moves 3" && { placed=1; break; }
    sleep 0.25
  done
  [ $placed = 1 ] && { echo "ok: arrows move the cursor, Space places again"; break; }
done
[ $placed = 1 ] || fail "arrows + Space did not place a third stone"
press 53  "overlay 1" "Escape opens Pause"
press 53  "overlay 0" "Escape closes Pause"
press 115 "scene 0"   "Home key goes to the main screen"
echo "E2E GUI (macOS): all checks passed"
