"""End-to-end test of the terminal version on Windows.

Runs caro_tui.exe inside a Windows pseudo-console (ConPTY, via pywinpty), so key presses go
through the real console input path (ReadConsoleInputW) and output through the VT renderer.
Output is fed into a VT emulator (pyte) and assertions run on the reconstructed screen,
because the game repaints only changed cells with cursor jumps. Same flow as tests/e2e_tui.sh.
Usage: python tests/e2e_tui_windows.py <path to caro_tui.exe>
"""
import os
import re
import sys
import tempfile
import threading
import time

import pyte  # pip install pyte
from winpty import PtyProcess  # pip install pywinpty

sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # screen dumps contain box glyphs
EXE = sys.argv[1]
SCALE = float(os.environ.get("XINJ_SCALE", "1"))
KEYS = {"Enter": "\r", "Down": "\x1b[B", "Up": "\x1b[A", "Right": "\x1b[C", "Left": "\x1b[D",
        "Escape": "\x1b", "Home": "\x1b[H", "Space": " "}

env = dict(os.environ, CARO_DATA_DIR=tempfile.mkdtemp(), CARO_AFK_IDLE="600")
proc = PtyProcess.spawn(EXE, dimensions=(32, 100), env=env)
screen = pyte.Screen(100, 32)
stream = pyte.Stream(screen)
lock = threading.Lock()


def reader():
    """PtyProcess.read() blocks until output arrives, so read on a background thread."""
    while True:
        try:
            data = proc.read(65536)
        except EOFError:
            return
        except Exception:
            return
        if data:
            with lock:
                stream.feed(data)


threading.Thread(target=reader, daemon=True).start()


def pump(seconds):
    time.sleep(seconds * SCALE)


def text():
    with lock:
        return "\n".join(screen.display)


def expect(pattern, what):
    for _ in range(12):
        pump(0.5)
        if re.search(pattern, text()):
            print("ok:", what)
            return
    print("E2E TUI (Windows) FAIL:", what, "- wanted", pattern)
    print(text())
    proc.terminate(force=True)
    sys.exit(1)


def keys(*names):
    for k in names:
        proc.write(KEYS[k])
        pump(0.25)


pump(1.5)
expect(r"Play", "home menu")
keys("Enter")
expect(r"New Game", "setup screen")
keys("Down", "Down", "Down", "Down", "Down", "Right", "Enter")
expect(r"Choose Your Champion", "profile screen")
keys("Down", "Down", "Right", "Enter")
expect(r"turn", "match started")
keys("Space")
expect(r"2 moves", "player placed, computer replied")
keys("Escape")
expect(r"Resume", "pause menu (Esc)")
keys("Escape", "Home")
expect(r"Continue", "Home key -> main menu")
keys("Down", "Enter")
expect(r"2 moves", "Continue resumes the match")
keys("Home", "Escape")
expect(r"Quit the game", "quit confirm")
keys("Left", "Enter")
for _ in range(10):
    pump(0.3)
    if not proc.isalive():
        break
if proc.isalive():
    print("E2E TUI (Windows) FAIL: did not quit")
    proc.terminate(force=True)
    sys.exit(1)
print("ok: quit")
print("E2E TUI (Windows): all checks passed")
