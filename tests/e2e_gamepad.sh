#!/usr/bin/env bash
# Gamepad end-to-end test (Linux, run as root: creates a virtual Xbox 360 pad via /dev/uinput).
# Drives both builds with the pad and asserts on what happened:
#   GUI  - via the CARO_TRACE log (scene / overlay / move-count changes)
#   TUI  - via the tmux screen
# Usage: sudo tests/e2e_gamepad.sh <build-dir>   (needs Xvfb, tmux, a C compiler)
set -euo pipefail
BUILD=$(cd "${1:-build}" && pwd)
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
S=${XINJ_SCALE:-1}
DISP=:$((80 + RANDOM % 9))
[ "$(id -u)" = 0 ] || { echo "run as root (needs /dev/uinput)"; exit 2; }
modprobe uinput 2>/dev/null || true
cc -O1 -o "$WORK/vpad" "$ROOT/tools/vpad.c"
Xvfb "$DISP" -screen 0 1280x720x24 >/dev/null 2>&1 & XPID=$!
cleanup() { pkill -P $$ caro 2>/dev/null || true; tmux -L padtest kill-server 2>/dev/null || true; kill $XPID 2>/dev/null || true; }
trap cleanup EXIT
ms() { echo "sleep $(( $1 * S ))"; }
fail() { echo "E2E GAMEPAD FAIL: $*"; echo "--- trace"; cat "$WORK"/trace*.txt 2>/dev/null || true; exit 1; }
want() { grep -qx "$2" "$1" || fail "$3 (no '$2' in $(basename "$1"))"; echo "ok: $3"; }

echo "== GUI menus: A selects, B goes back"
G1="cd $BUILD && env DISPLAY=$DISP CARO_DATA_DIR=$WORK/g1 CARO_TRACE=$WORK/trace1.txt ./caro >/dev/null 2>&1 &"
{ ms 1000; echo "cmd $G1"; ms 3500; echo a; ms 1200; echo b; ms 1200; } | "$WORK/vpad"
want "$WORK/trace1.txt" "scene 1" "pad A on Play opens New Game"
[ "$(grep '^scene' "$WORK/trace1.txt" | tail -1)" = "scene 0" ] || fail "pad B did not go back to Home"
echo "ok: pad B goes back to Home"
pkill -x caro || true; sleep 1

echo "== GUI match: D-pad moves, A places, B pause/close, Start -> Home"
G2="cd $BUILD && env DISPLAY=$DISP CARO_DATA_DIR=$WORK/g2 CARO_TRACE=$WORK/trace2.txt CARO_AFK_IDLE=600 ./caro >/dev/null 2>&1 &"
# Home: A (Play) -> Setup: Up Up (wraps to Next) A -> Profile: Up Up (Start) A -> match
{ ms 1000; echo "cmd $G2"; ms 3500; echo a; ms 1000; echo up; echo up; echo a; ms 1000
  echo up; echo up; echo a; ms 1500; echo right; echo a; ms 2000
  echo b; ms 900; echo b; ms 900; echo start; ms 1500; } | "$WORK/vpad"
want "$WORK/trace2.txt" "scene 5" "pad navigates Setup and Profile into a match"
want "$WORK/trace2.txt" "moves 1" "pad D-pad + A placed a stone"
want "$WORK/trace2.txt" "moves 2" "computer replied"
want "$WORK/trace2.txt" "overlay 1" "pad B opens Pause"
grep -A3 -x "overlay 1" "$WORK/trace2.txt" | grep -qx "overlay 0" || fail "pad B did not close Pause"
echo "ok: pad B closes Pause"
[ "$(grep '^scene' "$WORK/trace2.txt" | tail -1)" = "scene 0" ] || fail "pad Start did not go Home"
echo "ok: pad Start goes to the main screen"
pkill -x caro || true; sleep 1

echo "== Terminal version (Linux joystick API)"
cap() { echo "cmd tmux -L padtest capture-pane -p -t pt > $WORK/$1.txt"; }
T="tmux -L padtest new-session -d -s pt -x 100 -y 32 'env CARO_DATA_DIR=$WORK/t CARO_AFK_IDLE=600 $BUILD/caro_tui'"
{ ms 1000; echo "cmd $T"; ms 2500; cap t0; echo a; ms 1000; cap t1; echo b; ms 1000; cap t2
  echo a; ms 800; for i in 1 2 3 4 5; do echo down; done; echo right; echo a; ms 800
  echo down; echo down; echo right; echo a; ms 1200; cap t3
  echo a; ms 1800; cap t4; echo b; ms 800; cap t5; echo start; ms 1000; cap t6; } | "$WORK/vpad"
grep -q "New Game" "$WORK/t1.txt" || fail "TUI: pad A on Play did not open New Game"; echo "ok: TUI pad A selects"
grep -q "Play" "$WORK/t2.txt" && ! grep -q "New Game" "$WORK/t2.txt" || fail "TUI: pad B did not go back"; echo "ok: TUI pad B goes back"
grep -q "turn" "$WORK/t3.txt" || fail "TUI: pad could not start a match"; echo "ok: TUI pad navigates setup/profile and starts a match"
grep -q "2 moves" "$WORK/t4.txt" || fail "TUI: pad A did not place a stone"; echo "ok: TUI pad A places, computer replies"
grep -q "Resume" "$WORK/t5.txt" || fail "TUI: pad B did not open Pause"; echo "ok: TUI pad B opens Pause"
grep -q "Continue" "$WORK/t6.txt" || fail "TUI: pad Start did not go Home"; echo "ok: TUI pad Start goes Home"
echo "E2E GAMEPAD: all checks passed"
