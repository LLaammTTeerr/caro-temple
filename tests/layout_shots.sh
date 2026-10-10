#!/usr/bin/env bash
# Manual, offline layout check (not run in CI): renders every GUI scene - each board size,
# PvE and PvP, English and Vietnamese - plus a terminal-version capture of every screen,
# then builds contact sheets for a person to review.
# Usage: tests/layout_shots.sh <build-dir> [out-dir]   (Linux: needs Xvfb; ImageMagick for sheets)
set -euo pipefail
BUILD=$(cd "${1:-build}" && pwd)
OUT=$(mkdir -p "${2:-layout-shots}" && cd "${2:-layout-shots}" && pwd)
SCENES=("home" "setup" "profile" "profile --pvp" "settings" "saves" "game --board 3" "game --board 5 --pvp"
        "game --board 10" "pause" "surrender" "undo" "afk" "gameover --board 5")
for lang in 0 1; do
  DATA=$(mktemp -d)
  printf 'CARO-SETTINGS 1\nlang=%s\n' "$lang" > "$DATA/settings.cfg"
  for s in "${SCENES[@]}"; do
    n=$(echo "$s" | tr -d ' -')
    (cd "$BUILD" && CARO_DATA_DIR="$DATA" xvfb-run -a -s "-screen 0 1280x720x24" \
        ./caro --scene $s --frames 60 --shot "$OUT/gui-$lang-$n.png" >/dev/null 2>&1)
  done
done
if command -v montage >/dev/null; then
  for lang in 0 1; do
    montage "$OUT"/gui-$lang-*.png -tile 3x -geometry 640x360+4+4 -background '#111' "$OUT/sheet-gui-lang$lang.png"
  done
fi
# terminal version: one capture per screen via tmux
if command -v tmux >/dev/null; then
  SOCK=layout-$$; DATA=$(mktemp -d)
  t() { tmux -L "$SOCK" "$@"; }
  t new-session -d -s l -x 100 -y 32 "env CARO_DATA_DIR=$DATA CARO_AFK_IDLE=600 $BUILD/caro_tui"
  shot() { sleep 0.8; t capture-pane -p -t l > "$OUT/tui-$1.txt"; }
  shot home
  t send-keys -t l Enter;                                   shot setup
  t send-keys -t l Down Down Down Down Down Right Enter;    shot profile
  t send-keys -t l Down Down Right Enter;                   shot game
  t send-keys -t l Escape;                                  shot pause
  t kill-server
fi
echo "Layout screenshots in $OUT - review sheet-gui-lang0.png, sheet-gui-lang1.png and tui-*.txt"
