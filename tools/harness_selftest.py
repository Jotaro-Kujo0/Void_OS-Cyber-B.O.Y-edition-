#!/usr/bin/env python3
"""harness_selftest.py — automated smoke/flows test for the browser harness.

Builds the Pi-5 binary, serves it with web_harness.py on a random port, then:

  1. asserts the served page carries the simulation engine and the
     Chrome/Brave fallback transport (SIM map, simTick, modebtn, ?http);
  2. exercises the HTTP control surface (/cmd launch) and reads /poll;
  3. checks the same page serves fine in forced-HTTP mode (?http) — the
     path Brave Shields forces when it blocks the WebSocket.

Pure stdlib. Exit 0 on pass, 1 on failure. Run from the project root:
    python3 tools/harness_selftest.py [--build] [--port N]
"""

import argparse
import json
import pathlib
import socket
import subprocess
import sys
import time
import urllib.parse
import urllib.request
import urllib.error

ROOT = pathlib.Path(__file__).resolve().parent.parent
BIN = ROOT / "build" / "void_os_pi"
HARNESS = ROOT / "tools" / "web_harness.py"

PAGE_MARKERS = [
    "const SIM =",
    "function mountSim",
    "setInterval(simTick",
    "id=\"modebtn\"",
    "function startHttp",
    'data-app="${app.name}"',  # sim body injected at window-open time
    'id="aplist"',             # WIFI sim body present
    'id="nlogbox"',            # NFC sim body present
    'id="leaklog"',            # LEAK sim body present
]


def wait_http(url, timeout=25):
    """Poll GET url until it returns 200 or the device boots. Returns text."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            with urllib.request.urlopen(url, timeout=2) as r:
                return r.status, r.read().decode("utf-8", "replace")
        except Exception:
            time.sleep(0.5)
    raise RuntimeError("no HTTP response from " + url)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", action="store_true", help="force a cmake rebuild")
    ap.add_argument("--port", type=int, default=8133)
    args = ap.parse_args()

    # ── 0. build if asked or missing ────────────────────────────────────
    if args.build or not BIN.exists():
        print(f"[selftest] building host binary: {BIN}")
        subprocess.run(["cmake", "--build", str(ROOT / "build"), "-j4"],
                       check=True, cwd=ROOT, capture_output=True)
    if not BIN.exists():
        print("[selftest] FAIL: binary missing — build it with --build")
        return 1

    # ── 1. serve the harness ────────────────────────────────────────────
    url = f"http://127.0.0.1:{args.port}"
    srv = subprocess.Popen(
        [sys.executable, str(HARNESS), "--port", str(args.port), "--build"],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    proc = None
    fails = []
    try:
        print("[selftest] waiting for", url)
        code, page = wait_http(url)
        if code != 200:
            fails.append(f"GET {url} -> {code}")
        else:
            print(f"[selftest] page served, {len(page)} bytes")
            for m in PAGE_MARKERS:
                if m not in page:
                    fails.append(f"page missing marker: {m!r}")
                else:
                    print(f"[selftest] marker ok: {m}")

        # ── 2. HTTP control + telemetry flows ───────────────────────────
        # device boots on launch; keep sending until /poll reports alive.
        booted = False
        for _ in range(40):
            try:
                with urllib.request.urlopen(url + "/poll?since=0", timeout=2) as r:
                    j = json.loads(r.read().decode("utf-8"))
                if j.get("device_alive") is True or j.get("lines"):
                    booted = True
                    break
            except Exception:
                pass
            time.sleep(0.4)
        if not booted:
            fails.append("device never reported alive on /poll")
        else:
            print("[selftest] device alive via /poll")

        for app in ("SCAN", "WIFI", "NFC"):
            q = urllib.parse.quote("CMD launch " + app)
            with urllib.request.urlopen(url + "/cmd?line=" + q, timeout=2) as r:
                if r.status != 200:
                    fails.append(f"CMD launch {app} -> {r.status}")
                else:
                    print(f"[selftest] launch ok: {app}")

        # ── 3. forced-HTTP (?http) page — Brave-shield compatible path ──
        code2, page2 = wait_http(url + "/?http")
        ok_http_page = code2 == 200 and "const SIM =" in page2 and "?http" in page2 or \
                       code2 == 200 and "const SIM =" in page2
        if not (code2 == 200 and "const SIM =" in page2):
            fails.append(f"?http page missing sim engine (code {code2})")
        else:
            print("[selftest] ?http page OK")

    except Exception as e:
        fails.append(f"exception: {e}")
    finally:
        srv.terminate()
        try:
            srv.wait(timeout=5)
        except subprocess.TimeoutExpired:
            srv.kill()
        # child firmware may outlive the server
        subprocess.run(["pkill", "-f", "build/void_os_pi"],
                       capture_output=True, check=False)

    if fails:
        print("[selftest] FAIL:")
        for f in fails:
            print("   -", f)
        return 1
    print("[selftest] PASS ✔")
    return 0


if __name__ == "__main__":
    sys.exit(main())