"""End-to-end test of the terminal version on Windows.

Runs caro_tui.exe inside a Windows pseudo-console (ConPTY, via pywinpty), so key presses go
through the real console input path (ReadConsoleInputW) and output through the VT renderer.
Plays the same flow as tests/e2e_tui.sh and asserts on the screen text.
Usage: python tests/e2e_tui_windows.py <path to caro_tui.exe>
"""
import os
import re
import sys
import tempfile
import time

from winpty import PtyProcess  # pip install pywinpty

EXE = sys.argv[1]
SCALE = float(os.environ.get("XINJ_SCALE", "1"))
ANSI = re.compile(r"\x1b\[[0-9;?<>]*[ -/]*[@-~]|\x1b[()][0-9A-Za-z]|\x1b[=>78]|\x1b\][^\x07]*\x07")
KEYS = {"Enter": "\r", "Down": "\x1b[B", "Up": "\x1b[A", "Right": "\x1b[C", "Left": "\x1b[D",
        "Escape": "\x1b", "Home": "\x1b[H", "Space": " "}

env = dict(os.environ, CARO_DATA_DIR=tempfile.mkdtemp(), CARO_AFK_IDLE="600")
proc = PtyProcess.spawn(EXE, dimensions=(32, 100), env=env)
seen = ""  # all text received since the last check


def pump(seconds):
    global seen
    end = time.time() + seconds * SCALE
    while time.time() < end:
        try:
            if proc.isalive():
                data = proc.read(65536)
                if data:
                    seen += ANSI.sub("", data)
                    continue
        except EOFError:
            return
        time.sleep(0.02)


def expect(pattern, what):
    global seen
    for _ in range(12):
        pump(0.5)
        if re.search(pattern, seen):
            print("ok:", what)
            seen = ""
            return
    print("E2E TUI (Windows) FAIL:", what, "- wanted", pattern)
    print(seen[-3000:])
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
pump(1.5)
if proc.isalive():
    print("E2E TUI (Windows) FAIL: did not quit")
    proc.terminate(force=True)
    sys.exit(1)
print("ok: quit")
print("E2E TUI (Windows): all checks passed")
