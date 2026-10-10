#!/usr/bin/env bash
# End-to-end test of the GUI version on macOS: real key events through System Events
# (osascript), assertions through the CARO_TRACE log. Usage: tests/e2e_gui_macos.sh <build-dir>
set -euo pipefail
BUILD=$(cd "${1:-build}" && pwd)
WORK=$(mktemp -d)
S=${XINJ_SCALE:-1}
TRACE="$WORK/trace.txt"
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
front() { osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $GPID) to true" >/dev/null; }
key() { for k in "$@"; do osascript -e "tell application \"System Events\" to key code $k" >/dev/null; sleep "$(awk "BEGIN{print 0.25*$S}")"; done; }
start_game() {
  : > "$TRACE"
  (cd "$BUILD" && exec env CARO_DATA_DIR="$WORK/data" CARO_TRACE="$TRACE" CARO_AFK_IDLE=600 ./caro >"$WORK/game.log" 2>&1) & GPID=$!
  MARK=0; waitfor "scene 0" "window up (pid $GPID)"
  sleep "$(awk "BEGIN{print 1.5*$S}")"; front; sleep 1
}

echo "== menus and a match vs the computer"
start_game
mark; key 36;          waitfor "scene 1" "Return on Play opens New Game"
mark; key 53;          waitfor "scene 0" "Escape goes back to Home"
mark; key 36;          waitfor "scene 1" "New Game again"
mark; key 126 126 36;  waitfor "scene 2" "Up Up Return reaches Next -> Profile"
mark; key 126 126 36;  waitfor "scene 5" "Up Up Return reaches Start -> match"
mark; key 49;          waitfor "moves 1" "Space placed a stone"
                       waitfor "moves 2" "computer replied"
mark; key 124 125 49;  waitfor "moves 3" "arrows move the cursor, Space places again"
mark; key 53;          waitfor "overlay 1" "Escape opens Pause"
mark; key 53;          waitfor "overlay 0" "Escape closes Pause"
mark; key 115;         waitfor "scene 0" "Home key goes to the main screen"
echo "E2E GUI (macOS): all checks passed"
