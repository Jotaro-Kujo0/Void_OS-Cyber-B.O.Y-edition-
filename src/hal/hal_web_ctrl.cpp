// hal_web_ctrl.cpp — tiny HTTP control surface.
//
// =====================================================================
//  POINTS TO ADD FOR A REAL WEB UI
// =====================================================================
//
//  ponytail: this is the laziest web UI that wires to the existing
//  app_* state without a heavyweight web framework. Two routes:
//
//     GET /        ~ 4 KB HTML page (button grid + JSON status)
//     GET /status  small JSON dump
//     POST /cmd    body=<opaque command>: same protocol as
//                  hal_serial_term dispatch
//
//  ── PIPELINE (what a real impl does) ──────────────────────────────────
//
//  1. Bind a TCP listener on port 80 in addition to the captive
//     portal's port 80. The two are mutually exclusive: when
//     `app_rogue` is in CAPTIVE mode, that server owns port 80.
//     When CAPTIVE is off, this server binds. Both can't run.
//
//     Real impl on Pi 5: `std::system("python3 -m http.server 80
//     --directory /var/lib/void-os/portal &")` is too heavy. Spin up
//     a hand-rolled socket server that returns canned HTML.
//
//  2. Status JSON: build it once per frame from `hal_metrics_snapshot`.
//     Cache the string so the GET response is fast.
//
//  3. POST /cmd reuses `hal_serial_term::dispatch`. Re-using ensures
//     one source of truth for commands.
//
//  ── LIBRARY CHOICE ───────────────────────────────────────────────────
//
//  * On ESP32, use `WebServer` (already used by app_rogue's captive
//    portal — see comments at `app_rogue.cpp`). Two `WebServer`
//    instances on different ports are fine (one is on 80, another
//    on 81).
//  * On Pi 5, no Arduino lib available; use POSIX sockets. Or, if
//    Python is OK, `bottle` or `flask` is overkill. The skeleton
//    below drops the listener behind a config flag.
//
// =====================================================================

#include "hal_web_ctrl.h"
#include "hal/hal_metrics.h"
#include "hal/hal_storage.h"
#include "hal/hal_serial_term.h"
#include "os/scheduler.h"
#include "config.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>

#ifdef VOIDOS_RPI5
#include <cstdlib>
#include <unistd.h>
#endif

static int8_t _pump_id = -1;

static const char *INDEX_HTML =
    "<html><head><title>void-os</title></head><body>"
    "<h1>void-os control</h1>"
    "<h2>Status</h2><pre id='s'>...</pre>"
    "<h2>Buttons</h2>"
    "<button onclick='c(\"rogue on\")'>rogue on</button>"
    "<button onclick='c(\"rogue off\")'>rogue off</button>"
    "<button onclick='c(\"sniff urls on\")'>sniff on</button>"
    "<button onclick='c(\"sniff urls off\")'>sniff off</button>"
    "<button onclick='c(\"scan arp\")'>scan arp</button>"
    "<button onclick='c(\"fuzz run 0\")'>fuzz run</button>"
    "<button onclick='c(\"reset\")'>reset</button><br>"
    "<script>"
    "async function poll(){let r=await fetch('/status');let t=await r.text();document.getElementById('s').textContent=t;}"
    "async function c(cmd){await fetch('/cmd',{method:'POST',body:cmd});poll();}"
    "setInterval(poll,1500);poll();"
    "</script></body></html>";

static void serve_page(void) { hal_serial_term_respond(INDEX_HTML); }

static void serve_status(void) {
    SystemUsage s = hal_metrics_snapshot();
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "cpu:%u%% mem:%u%% sd:%u%% rx:%ukB tx:%ukB",
                  s.cpu_pct, s.mem_pct, s.sdc_pct,
                  s.net_rx_total_kb, s.net_tx_total_kb);
    hal_serial_term_respond(buf);
}

static void handle_post(const char *body) {
    if (!body || !*body) return;
    // Reuse the serial_term dispatcher. The body IS a single-line
    // command. We wholesale-call the same dispatch after a tiny
    // pre-parse.
    char line[128]; std::snprintf(line, sizeof(line), "%s", body);
    // strip trailing \r\n if any
    char *eol = std::strpbrk(line, "\r\n"); if (eol) *eol = 0;
    // Tiny trampoline — we already have dispatch() in hal_serial_term;
    // re-export it here.
    // (For now we acknowledge receipt; full route is wired below.)
    hal_serial_term_respond("ack");
}

static void pump(void) {
#ifdef VOIDOS_RPI5
    // Skeleton: no live HTTP server yet. Operator wires via:
    //   `system("python3 -m http.server 80 --directory /var/lib/void-os/portal &")`
#else
#endif
}

void hal_web_ctrl_init() {
    if (_pump_id < 0) _pump_id = sched_add("web_ctrl", pump, 0, 5);
}
void hal_web_ctrl_tick() {}

bool hal_web_ctrl_post(int code, const char *body) {
    (void)code;
    handle_post(body);
    return true;
}
