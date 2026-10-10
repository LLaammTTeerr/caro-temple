#!/usr/bin/env bash
# End-to-end test of the terminal version: plays through it inside tmux with real key input
# and asserts on what is on screen. Usage: tests/e2e_tui.sh <build-dir>  (needs tmux; Linux/macOS)
set -euo pipefail
BUILD=$(cd "${1:-build}" && pwd)
WORK=$(mktemp -d)
SOCK=caro-e2e-$$
S=${XINJ_SCALE:-1}
t() { tmux -L "$SOCK" "$@"; }
pause() { sleep "$(awk "BEGIN{print $1*$S}")"; }
screen() { t capture-pane -p -t e2e; }
expect() { # expect <regex> <what>
  for _ in 1 2 3 4 5 6; do screen | grep -qE "$1" && { echo "ok: $2"; return 0; }; pause 0.5; done
  echo "E2E TUI FAIL: $2 (wanted /$1/)"; screen; echo "--- key trace ---"; cat "$WORK/trace.txt" 2>/dev/null; exit 1
}
keys() {
  for k in "$@"; do
    t send-keys -t e2e "$k"; pause 0.15
    { echo "=== after key $k"; screen | grep -E "▶|◀|Champion|New Game|turn|moves" | head -4; } >> "$WORK/trace.txt"
  done
}
cleanup() { t kill-server 2>/dev/null || true; }
trap cleanup EXIT
cd "$WORK"
t new-session -d -s e2e -x 100 -y 32 "env CARO_DATA_DIR=$WORK/data CARO_AFK_IDLE=600 $BUILD/caro_tui"
pause 1
expect "Play" "home menu"
keys Enter;                    expect "New Game" "setup screen"
keys Down Down Down Down Down Right Enter; expect "Choose Your Champion" "profile screen"
keys Down Down Right Enter;    expect "turn" "match started"
keys Space; pause 1.5;         expect "2 moves" "player placed, computer replied"
keys Escape;                   expect "Resume" "pause menu (Esc)"
keys Escape; pause 0.4
screen | grep -q "Resume" && { echo "E2E TUI FAIL: Esc did not close pause"; exit 1; } || echo "ok: Esc closes pause"
keys Home;                     expect "Continue" "Home key -> main menu"
keys Down Enter;               expect "2 moves" "Continue resumes the match"
keys Home Escape;              expect "Quit the game" "quit confirm"
keys Left Enter; pause 0.5
t has-session -t e2e 2>/dev/null && { echo "E2E TUI FAIL: did not quit"; exit 1; } || echo "ok: quit"
echo "E2E TUI: all checks passed"
