#!/usr/bin/env python3
"""web_harness.py — browser-based counterpart to pc_harness.py.

Drive the void-os native (Raspberry Pi 5 / Linux) UI from a web browser,
no Pi hardware required. The page is a desktop: 12 app icons on each side, a big gear-headed
character in the centre (8-tooth gear head drawn procedurally on the
canvas, half-circle body, eye that follows the cursor) and click-to-open
draggable windows. Windows are
rendered in the browser with HTML/CSS/JS — the HELP app ships a full
cheatsheet, the other apps show live-or-dummy device stats — so they
work even with no device frames. Device stats and the serial log come
over a WebSocket by default; if the browser or network blocks
WebSockets the page auto-falls back to plain-HTTP polling (/poll, /cmd),
so the desktop works anywhere the page itself loads.

The browser page itself lives in tools/harness_page.html (the copy embedded
below is only a fallback if that file is missing).

It reuses the exact same host hooks as pc_harness, so no firmware changes
are needed:

    * VOIDOS_HARNESS=1   input is injected via stdin (a/A..c/C , . 0-9 q
                         — see src/hal/hal_input.cpp)
    * VOIDOS_DUMP_DIR    the binary writes frame_NNNNNN.ppm here
    * VOIDOS_DUMP_EVERY  Nth frame is dumped (higher = lighter load)
    * VOIDOS_CTRL_FIFO   REPL lines (`CMD ...` from the browser) go here

Run from the project root:

    python3 tools/web_harness.py             # auto-build then serve on :8000
    python3 tools/web_harness.py --port 9000
    python3 tools/web_harness.py --build      # force a native rebuild
    python3 tools/web_harness.py --every 3    # thin firmware PPM writes (1 per 3 frames)
    python3 tools/web_harness.py --fps 10     # cap the stream at 10 fps (encode+broadcast CPU)

Open http://127.0.0.1:8000/ in a browser. Click an icon to open that app
in a draggable window (✕ closes it and returns the device home). The
keyboard still drives the device directly: a/A b/B c/C = press/release,
,/. and up/down arrows = pot nudge, 0-9 = set pot coarse, q = quit.
"""

import argparse
import base64
import hashlib
import json
import os
import pathlib
import re
import shutil
import socket
import socketserver
import struct
import subprocess
import sys
import threading
import time
import urllib.parse
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Native binary candidates, in priority order.
BIN_CANDIDATES = [
    ROOT / ".pio/build/raspberrypi5/program",
    ROOT / "build/void_os_pi",
]
PIO = ROOT / ".venv/bin/pio"

SCR_W, SCR_H = 240, 320           # must match config.h
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

# Maps the firmware's coarse 0-9 pot presets to 0..255 (hal_input harness:
# digit maps to (c-'0')*255/9).
DIGIT_POT = {0: 0, 1: 28, 2: 57, 3: 85, 4: 113, 5: 142, 6: 170,
             7: 198, 8: 227, 9: 255}
HARNESS_ALPHABET = "abcABC.,0123456789q"     # everything harness_inject reads


# ── Pure-stdlib PNG encoder (no Pillow) ────────────────────────────────

def rgb_to_png(rgb: bytes, w: int = SCR_W, h: int = SCR_H) -> bytes:
    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return (struct.pack(">I", len(data)) + body
                + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))

    stride = w * 3
    raw = bytearray()
    for y in range(h):
        raw.append(0)                        # scanline filter: None
        raw += rgb[y * stride:(y + 1) * stride]
    z = zlib.compress(bytes(raw), 6)
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", ihdr) + chunk(b"IDAT", z) + chunk(b"IEND", b""))


def parse_ppm(blob: bytes):
    """Parse a P6 PPM emitted by TFT_eSPI::present(); returns (w, h, rgb)."""
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s*\n", blob, re.S)
    if not m:
        return None
    w, h, maxv = (int(m.group(i)) for i in (1, 2, 3))
    if maxv == 0 or w != SCR_W or h != SCR_H:
        return None
    data = blob[m.end():]
    return (w, h, data) if len(data) == w * h * 3 else None


# ── WebSocket framing (RFC 6455) ───────────────────────────────────────

def ws_send(sock: socket.socket, payload: bytes, opcode: int, lock):
    n = len(payload)
    frame = bytearray([0x80 | (opcode & 0x0F)])
    if n < 126:
        frame.append(n)
    elif n < 65536:
        frame.append(126)
        frame += struct.pack(">H", n)
    else:
        frame.append(127)
        frame += struct.pack(">Q", n)
    frame += payload
    with lock:
        sock.sendall(bytes(frame))


def ws_accept_key(key: str) -> str:
    digest = hashlib.sha1((key + WS_GUID).encode()).digest()
    return base64.b64encode(digest).decode()


def ws_recv(sock: socket.socket, buf: bytes):
    """Read one client message. Returns (opcode, payload, leftover) or
    (None, None, leftover) on EOF. Client frames are masked in transit."""
    while True:
        if len(buf) >= 2:
            b0, b1 = buf[0], buf[1]
            opcode, masked, ln = b0 & 0x0F, b1 & 0x80, b1 & 0x7F
            o = 2
            if ln == 126:
                if len(buf) < 4:
                    pass                          # need more bytes
                else:
                    ln = struct.unpack(">H", buf[2:4])[0]
                    o = 4
            elif ln == 127:
                if len(buf) < 10:
                    pass
                else:
                    ln = struct.unpack(">Q", buf[2:10])[0]
                    o = 10
            need = o + (4 if masked else 0) + ln
            if len(buf) >= need:
                mask = buf[o:o + 4] if masked else None
                data = bytearray(buf[o + (4 if masked else 0):need])
                if mask:
                    for i in range(len(data)):
                        data[i] ^= mask[i % 4]
                return opcode, bytes(data), buf[need:]
        chunk = sock.recv(4096)
        if not chunk:
            return None, None, buf
        buf += chunk


# ── Broker: shared state + fan-out ─────────────────────────────────────

class Broker:
    """Holds the latest frame + connected clients, plus a serial-log ring."""

    def __init__(self):
        self.lock = threading.Lock()
        self.clients = []
        self.latest_rgb = None      # freshest raw frame (cheap to cache)
        self.latest_png = None      # last encoded + streamed frame
        self.latest_png_ts = 0.0    # monotonic time latest_png was produced
        self.log_lines = []
        self.log_tail = 0
        self.last_stats = None        # last parsed "STATS ..." line, replayed on join
        self.frame_seq = 0            # count of consumed frames (http polling)

    def fresh_png(self, max_age: float = 0.5):
        """A PNG good for an immediate paint. Returns the last streamed frame
        if it's recent; otherwise lazily encodes the freshest raw frame once,
        so a client that connects between throttled sends still gets the
        current screen without pushing the watcher past its FPS budget."""
        now = time.monotonic()
        with self.lock:
            if self.latest_png is not None and now - self.latest_png_ts < max_age:
                return self.latest_png
            if self.latest_rgb is not None:
                png = rgb_to_png(self.latest_rgb)
                self.latest_png = png
                self.latest_png_ts = now
                return png
        return None

    def add_log(self, text: str):
        with self.lock:
            for ln in text.splitlines():
                if len(self.log_lines) >= 500:
                    self.log_lines.pop(0)
                self.log_lines.append(ln)
            self.log_tail = len(self.log_lines)

    def poll_log(self, idx: int):
        with self.lock:
            return self.log_lines[idx:], self.log_tail

    def broadcast(self, payload: bytes, opcode: int):
        with self.lock:
            for c in list(self.clients):
                try:
                    c.send(payload, opcode)
                except OSError:
                    try:
                        self.clients.remove(c)
                    except ValueError:
                        pass


class Conn:
    """A connected WebSocket client."""

    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.send_lock = threading.Lock()
        self.closed = False

    def send(self, payload: bytes, opcode: int):
        if self.closed:
            raise OSError("socket closed")
        ws_send(self.sock, payload, opcode, self.send_lock)

    def close(self, code: int = 1000):
        self.closed = True
        try:
            ws_send(self.sock, struct.pack(">H", code), 8, self.send_lock)
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass


def conn_input_loop(conn: Conn, broker: Broker, forwarder):
    """Serve one WebSocket connection: push frames, forward typed keys."""
    try:
        print("[web] ws client connected", flush=True)
        png = broker.fresh_png()
        if png:
            conn.send(png, 2)                    # paint current screen at once
        if broker.last_stats:
            conn.send(broker.last_stats.encode(), 1)
    except OSError:
        conn.closed = True
    with broker.lock:
        broker.clients.append(conn)
    buf = b""
    try:
        while True:
            op, payload, buf = ws_recv(conn.sock, buf)
            if op is None:
                break
            if op == 0x8:                        # close
                break
            if op == 0x9:                        # ping -> pong
                try:
                    conn.send(payload, 0xA)
                except OSError:
                    break
                continue
            if op in (0x1, 0x2) and payload:
                forwarder.handle(payload)
    except OSError:
        pass
    finally:
        conn.closed = True
        with broker.lock:
            try:
                broker.clients.remove(conn)
            except ValueError:
                pass
        try:
            conn.sock.close()
        except OSError:
            pass
        print("[web] ws client disconnected", flush=True)


# ── Frame capture ──────────────────────────────────────────────────────

class FrameWatcher(threading.Thread):
    """Consumes the PPM dump dir and streams frames to browsers.

    It consumes *every* new PPM as soon as it appears (so disk usage stays
    flat no matter how fast the device renders) and caches the freshest raw
    frame for an instant paint on connect, but it only re-encodes +
    broadcasts at most `fps` times per second. The encode + WebSocket
    delivery is the expensive work, and this caps it no matter how fast the
    operator spins the pot.

    The device's dump counter restarts from 0 on firmware restart, so a
    counter going backwards is treated as a fresh child and re-anchored;
    otherwise the stream would stall forever after the RESTART button.
    """

    def __init__(self, dump_dir: pathlib.Path, broker: Broker, fps: float = 15.0):
        super().__init__(daemon=True)
        self.dump_dir = dump_dir
        self.broker = broker
        self.interval = 1.0 / max(1.0, fps)
        self.tick = min(0.02, self.interval)     # poll a bit faster than sending
        self._stop = threading.Event()
        self._last = -1                          # highest counter consumed so far

    def stop(self):
        self._stop.set()

    def _collect(self) -> list:
        """Sorted counters of every frame file currently on disk. Anything
        that shows up here is consumed in `run` — that's what keeps the
        dump dir flat regardless of how many frames pile up per tick."""
        found = []
        try:
            names = os.listdir(self.dump_dir)
        except OSError:
            return found
        for name in names:
            m = re.match(r"frame_(\d+)\.ppm$", name)
            if m:
                found.append(int(m.group(1)))
        found.sort()
        return found

    def run(self):
        time.sleep(0.4)                          # let the child draw its splash
        last_send = 0.0
        while not self._stop.wait(self.tick):
            counters = self._collect()
            if not counters:
                continue
            # A counter going backwards means the firmware was restarted
            # and its dump counter reset; re-anchor below everything
            # present so the new child's frames are all picked up.
            if counters[-1] < self._last:
                self._last = -1
            fresh = None
            for c in counters:
                path = self.dump_dir / f"frame_{c:06d}.ppm"
                try:
                    parsed = parse_ppm(path.read_bytes())
                except OSError:
                    continue
                finally:
                    try:
                        path.unlink()
                    except OSError:
                        pass
                if not parsed:
                    continue
                fresh = parsed[2]
                self._last = c
            if fresh is None:
                continue
            now = time.monotonic()
            with self.broker.lock:
                self.broker.latest_rgb = fresh   # always: freshest frame cached
                self.broker.frame_seq += 1
                has_clients = bool(self.broker.clients)
            # Encode + push only within budget (and only when anyone's watching).
            if has_clients and now - last_send >= self.interval:
                png = rgb_to_png(fresh)
                with self.broker.lock:
                    self.broker.latest_png = png
                    self.broker.latest_png_ts = now
                self.broker.broadcast(png, 2)
                last_send = now


# ── Child process ──────────────────────────────────────────────────────

class Child:
    def __init__(self, binary: pathlib.Path, dump_dir: pathlib.Path,
                 every: int, on_log, on_exit, ctrl_fifo=None):
        self.binary = binary
        self.dump_dir = dump_dir
        self.every = every
        self.on_log = on_log
        self.on_exit = on_exit
        self.ctrl_fifo = ctrl_fifo
        self.ctrl = None             # write end of the control FIFO
        self.ctrl_lock = threading.Lock()
        self.proc = None
        self.stdin_lock = threading.Lock()
        self.gen = 0                 # spawn generation; lets on_exit tell a
                                     # deliberate restart from a real exit

    def spawn(self):
        self.gen += 1
        gen = self.gen
        env = dict(os.environ)
        env["VOIDOS_HARNESS"] = "1"
        env["VOIDOS_DUMP_DIR"] = str(self.dump_dir)
        env["VOIDOS_DUMP_EVERY"] = str(self.every)
        if self.ctrl_fifo:
            env["VOIDOS_CTRL_FIFO"] = str(self.ctrl_fifo)
        env.pop("VOIDOS_DUMP_ASCII", None)       # keep stderr quiet
        proc = subprocess.Popen(
            [str(self.binary)], cwd=ROOT, env=env,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        self.proc = proc
        if self.ctrl_fifo is not None and not self.ctrl:
            threading.Thread(target=self._open_ctrl, daemon=True).start()
        threading.Thread(target=self._drain_stream, args=(proc,),
                         daemon=True).start()
        threading.Thread(target=self._watch_status, args=(proc, gen),
                         daemon=True).start()

    def restart(self):
        """Kill the current child and boot a fresh one. Bumping gen first
        makes the old process's exit callback a no-op, so a deliberate
        restart never broadcasts "__STOPPED__" to browsers."""
        self.gen += 1
        self.kill()
        self.spawn()

    def _open_ctrl(self):
        # O_WRONLY open blocks only until the child opens the read end (it uses
        # O_NONBLOCK, so it opens immediately). Kept open across restarts.
        try:
            fd = os.open(str(self.ctrl_fifo), os.O_WRONLY)
        except OSError:
            return
        self.ctrl = os.fdopen(fd, "w", buffering=1)

    def send_command(self, cmd: str):
        """Send a REPL command over the control FIFO (not keyboard stdin)."""
        if not cmd.endswith("\n"):
            cmd += "\n"
        c = self.ctrl
        if not c:
            return
        with self.ctrl_lock:
            try:
                c.write(cmd)
                c.flush()
            except (BrokenPipeError, OSError):
                self.ctrl = None

    def _drain_stream(self, proc):
        assert proc and proc.stdout
        while True:
            chunk = proc.stdout.readline()
            if not chunk:
                break
            self.on_log(chunk.decode("utf-8", "replace"))
        try:
            self.on_log("\n[serial link closed]\n")
        except Exception:
            pass

    def _watch_status(self, proc, gen):
        code = proc.wait()
        self.on_exit(code, gen)

    def send_bytes(self, data: bytes):
        if not self.proc or self.proc.poll() is not None or not self.proc.stdin:
            return
        with self.stdin_lock:
            try:
                self.proc.stdin.write(data)
                self.proc.stdin.flush()
            except (BrokenPipeError, OSError):
                pass

    def kill(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.proc.kill()


class InputForwarder:
    """Routes browser messages: CMD lines to the control FIFO, keycodes to
    keyboard stdin."""

    def __init__(self, child: Child, on_restart):
        self.child = child
        self.on_restart = on_restart

    def handle(self, payload: bytes):
        text = payload.decode("utf-8", "replace")
        if "\x1bRESTART" in text:
            self.on_restart()
            return
        if text.startswith("CMD "):
            self.child.send_command(text[4:])
            return
        keep = [c for c in text if c in HARNESS_ALPHABET]
        if keep:
            self.child.send_bytes("".join(keep).encode())


# ── HTTP + WebSocket request handler ───────────────────────────────────

# The page is served from tools/harness_page.html (single source of
# truth). No embedded clone here; if that page ever goes missing we return
# a clear error instead of a silently-old copy.
def _load_page() -> str:
    page = ROOT / "tools" / "harness_page.html"
    if page.is_file():
        return page.read_text(encoding="utf-8")
    return ("<!doctype html><html><body style='background:#07090e;color:#41e6a6;"
            "font-family:monospace;padding:40px'><h1>Void-OS harness</h1>"
            "<p>tools/harness_page.html is missing - restore it with "
            "git checkout tools/harness_page.html</p></body></html>")


PAGE_HTML = _load_page()


def host_sysinfo():
    """Real host info for the browser app sims (SYS/STAT windows).

    Cheap /proc + /sys reads, no dependencies; the browser falls back to
    its fake sims when this is missing or fields are absent (e.g. no
    thermal zone on this machine).
    """
    info = {"hostname": socket.gethostname() or "unknown",
            "cores": os.cpu_count() or 0}

    def first(path):
        try:
            with open(path) as f:
                return f.read().strip()
        except OSError:
            return ""

    cpuinfo = first("/proc/cpuinfo")
    for line in cpuinfo.splitlines():
        if line.lower().startswith("model name"):
            info["cpu_model"] = line.split(":", 1)[1].strip()
            break
    up = first("/proc/uptime")
    if up:
        try:
            info["uptime"] = float(up.split()[0])
        except (ValueError, IndexError):
            pass
    mem = {}
    for line in first("/proc/meminfo").splitlines():
        k, _, v = line.partition(":")
        try:
            mem[k] = int(v.split()[0]) * 1024
        except (ValueError, IndexError):
            pass
    if mem.get("MemTotal"):
        info["mem_total"] = mem["MemTotal"]
        info["mem_avail"] = mem.get("MemAvailable", mem.get("MemFree", 0))
    load = first("/proc/loadavg").split()
    if load:
        try:
            info["load1"] = float(load[0])
        except ValueError:
            pass
    temp = first("/sys/class/thermal/thermal_zone0/temp")
    if temp:
        try:
            info["temp_c"] = int(temp) / 1000.0
        except ValueError:
            pass
    return json.dumps(info)


class Handler(socketserver.BaseRequestHandler):
    """Serves the HTML page and upgrades /ws to a WebSocket per connection."""

    def handle(self):
        header = b""
        try:
            while b"\r\n\r\n" not in header:
                chunk = self.request.recv(4096)
                if not chunk:
                    return
                header += chunk
                if len(header) > 64 * 1024:
                    self._respond(431, b"request too large")
                    return
            head, _, rest = header.partition(b"\r\n\r\n")
            request_line = head.split(b"\r\n")[0].decode("latin1", "replace")
            parts = request_line.split()
            verb = parts[0] if parts else ""
            path = parts[1] if len(parts) > 1 else "/"
            headers = {}
            for hl in head.split(b"\r\n")[1:]:
                if b":" in hl:
                    k, _, v = hl.partition(b":")
                    headers[k.strip().lower().decode("latin1", "replace")] = \
                        v.strip().decode("latin1", "replace")
        except OSError:
            return

        # Surface every request on the harness console so a browser whose
        # WebSocket never opens is easy to diagnose (does the /ws upgrade
        # even reach the server?).
        print(f"[web] {verb} {path} from {self.client_address[0]}", flush=True)

        # WebSocket upgrade for the live screen + input channel.
        server = self.server
        if path == "/ws" and headers.get("upgrade", "").lower() == "websocket":
            accept = ws_accept_key(headers.get("sec-websocket-key", ""))
            self.request.sendall(
                ("HTTP/1.1 101 Switching Protocols\r\n"
                 "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                 f"Sec-WebSocket-Accept: {accept}\r\n\r\n").encode())
            conn = Conn(self.request)
            conn_input_loop(conn, server.broker, server.forwarder)
            return

        if verb != "GET":
            self._respond(405, b"method not allowed")
        elif path == "/favicon.ico":
            self._respond(204, b"")
        elif path == "/frame.png":
            # HTTP fallback for browsers whose WebSocket is blocked: serve
            # the freshest screen as a PNG (204 until the first frame).
            png = server.broker.fresh_png(max_age=5.0)
            if png:
                self._respond(200, png, "image/png")
            else:
                self._respond(204, b"")
        elif path.startswith("/cmd"):
            # HTTP fallback for input: same dispatch as a WS message.
            qs = urllib.parse.parse_qs(urllib.parse.urlparse(path).query)
            line = qs.get("line", [""])[0]
            if line:
                server.forwarder.handle(line.encode("utf-8", "replace"))
            self._respond(200, b"ok")
        elif path.startswith("/assets/"):
            # Static UI assets (e.g. mascot head JPEGs) from src/UI/assets/.
            name = os.path.basename(path)
            asset = ROOT / "src" / "UI" / "assets" / name
            if asset.is_file():
                ctype = ("image/png" if name.lower().endswith(".png")
                         else "application/octet-stream")
                self._respond(200, asset.read_bytes(), ctype)
            else:
                self._respond(404, b"not found")
        elif path == "/sysinfo":
            # Real machine info for the SYS/STAT app windows.
            self._respond(200, host_sysinfo().encode(), "application/json")
        elif path.startswith("/poll"):
            # HTTP fallback for the serial log: lines since `since` + stats
            # + whether the device process is actually alive.
            qs = urllib.parse.parse_qs(urllib.parse.urlparse(path).query)
            try:
                since = int(qs.get("since", ["0"])[0])
            except ValueError:
                since = 0
            lines, tail = server.broker.poll_log(since)
            child = server.forwarder.child
            proc = child.proc
            alive = bool(proc and proc.poll() is None)
            body = json.dumps({
                "since": tail, "lines": lines,
                "stats": server.broker.last_stats,
                "frames": server.broker.frame_seq,
                "device_alive": alive,
                "device_exit": None if alive else (proc.poll() if proc else "never-started"),
            }).encode()
            self._respond(200, body, "application/json")
        else:
            self._respond(200, PAGE_HTML.encode(), "text/html; charset=utf-8")

    def _respond(self, code, body, ctype="text/plain"):
        reason = {200: "OK", 204: "No Content", 405: "Method Not Allowed",
                  431: "Request Header Fields Too Large"}.get(code, "OK")
        head = (f"HTTP/1.1 {code} {reason}\r\nContent-Type: {ctype}\r\n"
                f"Content-Length: {len(body)}\r\nCache-Control: no-store\r\n"
                "Connection: close\r\n\r\n").encode()
        try:
            self.request.sendall(head + body)
        except OSError:
            pass


# ── Build + wiring ─────────────────────────────────────────────────────

def _binary_is_stale(binary) -> bool:
    """True if any source .cpp is newer than the native binary (so the PPM/dump
    and app changes it embeds are present). Avoids silently running an old build."""
    if not (binary and binary.is_file()):
        return False
    bin_mtime = binary.stat().st_mtime
    src = ROOT / "src"
    if not src.is_dir():
        return False
    for p in src.rglob("*.cpp"):
        try:
            if p.stat().st_mtime > bin_mtime:
                return True
        except OSError:
            continue
    return False


def find_binary(explicit):
    if explicit:
        return pathlib.Path(explicit)
    for cand in BIN_CANDIDATES:
        if cand.is_file() and os.access(cand, os.X_OK) and not _binary_is_stale(cand):
            return cand
    return None


def build(binary) -> pathlib.Path:
    if binary and binary.is_file() and os.access(binary, os.X_OK) \
            and not _binary_is_stale(binary):
        return pathlib.Path(binary)
    if PIO.is_file():
        print(f"[harness] building via PlatformIO -> {BIN_CANDIDATES[0]}", flush=True)
        subprocess.run([str(PIO), "run", "-e", "raspberrypi5"],
                       cwd=ROOT, check=True)
        return BIN_CANDIDATES[0]
    if (ROOT / "CMakeLists.txt").is_file():
        builddir = ROOT / "build"
        if not (builddir / "CMakeCache.txt").is_file():
            print("[harness] configuring CMake build -> build/", flush=True)
            builddir.mkdir(parents=True, exist_ok=True)
            subprocess.run(["cmake", "-S", str(ROOT), "-B", str(builddir)],
                           cwd=ROOT, check=True)
        print("[harness] building via CMake -> build/void_os_pi", flush=True)
        subprocess.run(["cmake", "--build", str(builddir), "-j4"],
                       cwd=ROOT, check=True)
        return builddir / "void_os_pi"
    raise SystemExit("[harness] no build tool found; run .venv/bin/pio "
                     "pkg install -e raspberrypi5 or configure build/ ")


def main():
    ap = argparse.ArgumentParser(description="Void-OS web harness (browser UI)")
    ap.add_argument("--binary", help="path to the native void-os binary")
    ap.add_argument("--build", action="store_true", help="force a rebuild")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--every", type=int, default=2,
                    help="dump one frame each N device frames (default 2)")
    ap.add_argument("--fps", type=float, default=15.0,
                    help="max frames/s streamed to the browser (1..60; "
                         "default 15) — caps encode+broadcast CPU even while "
                         "moving the pot rapidly")
    ap.add_argument("--dump-dir", default=None,
                    help="dir for the device PPM frame dumps (default: "
                         "project .webharness-<pid>)")
    args = ap.parse_args()
    if args.every < 1:
        ap.error("--every must be >= 1")
    if not 1 <= args.fps <= 60:
        ap.error("--fps must be between 1 and 60")

    binary = find_binary(args.binary)
    if args.build or binary is None:
        binary = build(binary)
    binary = pathlib.Path(binary)
    print(f"[harness] binary: {binary}", flush=True)

    in_project = args.dump_dir is None
    dump_dir = pathlib.Path(args.dump_dir) if args.dump_dir \
        else ROOT / f".webharness-{os.getpid()}"
    dump_dir.mkdir(parents=True, exist_ok=True)
    ctrl_fifo = dump_dir / "ctrl.fifo"
    try:
        os.mkfifo(ctrl_fifo)                     # control/REPL channel
    except FileExistsError:
        pass

    broker = Broker()

    _STAT = re.compile(r"cpu:(\d+)% mem:(\d+)% sd:(\d+)% rx:(\d+)kB")

    def on_log(text):
        broker.add_log(text)
        m = _STAT.search(text)
        if m:
            stats = "STATS cpu=%s mem=%s sd=%s rx=%s" % m.groups()
            broker.last_stats = stats
            broker.broadcast(stats.encode(), 1)
        else:
            # Everything else (boot lines, REPL replies like "launch: MARAUD")
            # goes to the browser log drawer too.
            line = text.rstrip("\n")
            if line:
                broker.broadcast(line.encode("utf-8", "replace"), 1)

    def on_exit(code, gen):
        if gen != child.gen:
            return                  # superseded by a restart; new child runs
        print(f"[harness] firmware exited ({code})", flush=True)
        broker.broadcast(b"\n[device process exited]\n", 1)
        broker.broadcast("__STOPPED__".encode(), 1)

    def restart():
        print("[harness] restarting firmware", flush=True)
        child.restart()

    child = Child(binary, dump_dir, args.every, on_log, on_exit, ctrl_fifo)
    forwarder = InputForwarder(child, restart)

    def poll_stats():
        # Always ask the device for "status" once a second so live stats
        # are available both to WebSocket clients and to browsers stuck
        # on the HTTP-polling fallback (last_stats is replayed via /poll).
        while True:
            time.sleep(1.0)
            child.send_command("status")

    threading.Thread(target=poll_stats, daemon=True).start()

    watcher = FrameWatcher(dump_dir, broker, fps=args.fps)
    child.spawn()
    watcher.start()

    class Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
        daemon_threads = True
        allow_reuse_address = True

    server = Server(("0.0.0.0", args.port), Handler)
    server.broker = broker
    server.forwarder = forwarder

    # Ensure the child firmware is terminated (not orphaned) on Ctrl-C *and*
    # on an external SIGTERM, so a killed harness never leaves a zombie loop.
    def handling_stop(signum, frame):
        threading.Thread(target=server.shutdown, daemon=True).start()

    import signal
    prev_term = signal.signal(signal.SIGTERM, handling_stop)
    signal.signal(signal.SIGINT, handling_stop)

    print(f"[harness] open http://127.0.0.1:{args.port}/ in a browser", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        signal.signal(signal.SIGTERM, prev_term)
        child.kill()
        watcher.stop()
        if in_project:
            shutil.rmtree(dump_dir, ignore_errors=True)
        print("\n[harness] stopped", flush=True)


if __name__ == "__main__":
    sys.exit(main())