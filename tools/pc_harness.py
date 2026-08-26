#!/usr/bin/env python3
"""PC harness: see and drive the void-os UI on a PC (no Pi hardware).

Run from the project root:
    python3 tools/pc_harness.py          # run (builds first if binary missing)
    python3 tools/pc_harness.py --build  # force a build first

Keys:
    a / b / c         button A/B/C press     A / B / C   release
    , / .             pot nudge -/+16        up / down arrows  same
    0-9               set pot (0..255)
    q                 quit

The screen renders to the terminal as ANSI half-blocks. The program's own
stdout (Serial logs) stays visible above it. Exit always restores the
terminal.
"""

import os
import pathlib
import select
import subprocess
import sys
import termios
import tty

ROOT = pathlib.Path(__file__).resolve().parent.parent
BIN = ROOT / ".pio/build/raspberrypi5/program"
PIO = ROOT / ".venv/bin/pio"


def translate(pending: bytes):
    """Translate complete input tokens; return (forwarded, held_back)."""
    out = bytearray()
    i = 0
    n = len(pending)
    while i < n:
        b = pending[i]
        if b == 0x1B:  # ESC sequence
            if i + 2 < n and pending[i + 1] == ord("["):
                k = pending[i + 2]
                if k in (ord("A"), ord("B")):      # up/down -> pot
                    out.append(ord(",") if k == ord("A") else ord("."))
                    i += 3
                    continue
                if k in (ord("C"), ord("D")):      # left/right: ignore
                    i += 3
                    continue
                j = i + 3                           # other CSI: skip to final byte
                while j < n and not (0x40 <= pending[j] <= 0x7E):
                    j += 1
                i = j + 1 if j < n else n
                continue
            if i + 1 >= n:                          # lone ESC: wait for rest
                break
            i += 1
            continue
        out.append(b)
        i += 1
    return bytes(out), pending[i:]


def main() -> int:
    if "--build" in sys.argv or not BIN.exists():
        print("[harness] building raspberrypi5 target ...", flush=True)
        subprocess.run([str(PIO), "run", "-e", "raspberrypi5"], cwd=ROOT, check=True)

    env = dict(os.environ)
    env["VOIDOS_HARNESS"] = "1"
    env["VOIDOS_DUMP_ASCII"] = "1"
    env.setdefault("VOIDOS_DUMP_EVERY", "2")   # preview refresh every 2nd frame

    proc = subprocess.Popen([str(BIN)], cwd=ROOT, env=env, stdin=subprocess.PIPE)

    fd = sys.stdin.fileno()
    interactive = sys.stdin.isatty()
    old = termios.tcgetattr(fd) if interactive else None
    pending = b""
    try:
        if interactive:
            tty.setraw(fd)
            sys.stderr.write("\x1b[2J")
            sys.stderr.write(
                "[harness] a/A b/B c/C = press/release   , . arrows = pot  0-9 = set  q = quit\n")
            sys.stderr.flush()
        while proc.poll() is None:
            r, _, _ = select.select([sys.stdin], [], [], 0.2)
            if not r:
                continue
            chunk = os.read(fd, 64)
            if not chunk:
                break
            pending += chunk
            fwd, pending = translate(pending)
            if fwd:
                try:
                    proc.stdin.write(fwd)
                    proc.stdin.flush()
                except BrokenPipeError:
                    break
    finally:
        if interactive:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)
            sys.stderr.write("\x1b[?25h\x1b[0m\n")
            sys.stderr.flush()
    return proc.wait()


if __name__ == "__main__":
    sys.exit(main())
