// app_scan.cpp — small passive recon.
//
// State machine per row:
//
//   IDLE → ARMED → RUNNING → IDLE.
//   RUNNING sweeps the work so the screen keeps painting each frame.
//   ARP walks /proc/net/arp (Pi 5) or the soft-AP station list (ESP32).
//   TCP scans 24 high-yield ports 1–2 per frame so the user can still
//   navigate away the moment they're done.
//
// Sub-modes:
//   0. ARP HOSTS
//   1. TCP  PORTS
//   2. SERVICES  (port → name hint, scroll with POT)
//   3. WIFI HOSTS  (soft-AP station list)

#include "app_scan.h"
#include "../UI/draw.h"
#include "../UI/theme.h"
#include "../hal/hal_storage.h"
#include "../config.h"
#include <cstdio>
#include <cstring>

#ifdef VOIDOS_RPI5
#include <fstream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#endif

#define SCAN_LOG_MAX  32
#define SCAN_PORT_LOG_MAX 24

static char _host_log[SCAN_LOG_MAX][40];
static uint16_t _host_n = 0;
static char _port_log[SCAN_PORT_LOG_MAX][18];
static uint16_t _port_n = 0;

static inline void log_host(const char *mac, const char *ip) {
    if (_host_n >= SCAN_LOG_MAX) return;
    std::snprintf(_host_log[_host_n++], 40, "%-17s %s", mac, ip);
}
static inline void log_port(const char *row) {
    if (_port_n >= SCAN_PORT_LOG_MAX) return;
    std::snprintf(_port_log[_port_n++], 18, "%s", row);
}

// ── Service hint dict ──────────────────────────────────────────────────

struct Hint { uint16_t port; const char *name; };
static const Hint HINTS[] = {
    {22,"ssh"}, {23,"telnet"}, {25,"smtp"}, {53,"dns"},
    {80,"http"}, {110,"pop3"}, {139,"netbios"}, {143,"imap"},
    {389,"ldap"}, {443,"https"}, {445,"smb"}, {587,"smtps"},
    {1433,"mssql"}, {3306,"mysql"}, {3389,"rdp"}, {5000,"flask"},
    {5432,"pgsql"}, {5900,"vnc"}, {6379,"redis"}, {8000,"django"},
    {8080,"http-alt"}, {8443,"https-alt"}, {9200,"elastic"}, {27017,"mongo"},
};
static const uint8_t N_HINTS = sizeof(HINTS)/sizeof(HINTS[0]);

// ── UI + state ──────────────────────────────────────────────────────────

static const char *LABELS[] = { "ARP HOSTS", "TCP  PORTS", "SERVICES", "WIFI HOSTS" };
static const uint8_t N_ROWS = sizeof(LABELS)/sizeof(LABELS[0]);
static uint8_t _row = 0, _hint_scroll = 0;
static char _status[24] = "READY";
static uint8_t _phase = 0;       // 0=IDLE 1=ARMED-but-not-yet  2=RUNNING 3=DONE
static uint8_t _run_i = 0;       // TCP scan port index
static char  _target[24] = "192.168.1.1";

// ── Persistence — seed scan_target on first run ─────────────────────────

static void load_target() {
    char buf[24];
    if (!hal_storage_get_str("scan_target", buf, sizeof(buf)))
        std::snprintf(buf, sizeof(buf), "192.168.1.1");
    std::snprintf(_target, sizeof(_target), "%s", buf);
    hal_storage_set_str("scan_target", _target);
}

// ── Port pool used by both targets ──────────────────────────────────────

static const uint16_t PORTS[] = {
    22,23,25,53,80,110,139,143,389,443,445,587,
    1433,3306,3389,5000,5432,5900,6379,8000,8080,8443,9200,27017
};
static const uint8_t N_PORTS = sizeof(PORTS)/sizeof(PORTS[0]);

// ── ARP SCAN per platform ───────────────────────────────────────────────

#ifdef VOIDOS_RPI5
static void do_arp() {
    _host_n = 0;
    std::ifstream f("/proc/net/arp");
    if (!f) { std::snprintf(_status,sizeof(_status),"arp: read fail"); return; }
    char line[256]; f.getline(line, sizeof(line)); // header
    while (f.getline(line, sizeof(line)) && _host_n < SCAN_LOG_MAX) {
        char ip[20]={}, mac[20]={};
        std::sscanf(line, "%19s %*s %*s %19s", ip, mac);
        if (ip[0] && mac[0] && std::strcmp(mac, "00:00:00:00:00:00")) log_host(mac, ip);
    }
    std::snprintf(_status,sizeof(_status),"ARP: %u host", _host_n);
}

// Scan next port index `i`; returns true on success. Errors aren't fatal.
static bool do_tcp_one(uint8_t i) {
    if (i >= N_PORTS) return true; // sweep done, caller exits
    if (_port_n >= SCAN_PORT_LOG_MAX) return true;
    uint16_t p = PORTS[i];
    int s = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (s < 0) return false;
    struct sockaddr_in sa{};
    sa.sin_family = AF_INET; sa.sin_port = htons(p);
    if (::inet_pton(AF_INET, _target, &sa.sin_addr) != 1) {
        std::snprintf(_status, sizeof(_status), "%.20s ?", _target);
        ::close(s); return false;
    }
    if (::connect(s, (struct sockaddr*)&sa, sizeof(sa)) == 0 ||
        errno == EINPROGRESS) {
        struct timeval tv{0, 80000};
        ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        int err = 0; socklen_t sl = sizeof(err); ::getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &sl);
        char row[18]; std::snprintf(row, sizeof(row), "%-5u %s", p, err ? "CLOSED" : "OPEN");
        log_port(row);
    }
    ::close(s);
    return false;
}
#else
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_netif.h>
static void do_arp() {
    _host_n = 0;
    wifi_sta_list_t wl = {};
    if (esp_wifi_ap_get_sta_list(&wl) != ESP_OK) {
        std::snprintf(_status, sizeof(_status), "AP STA: none");
        return;
    }
    esp_netif_sta_list_t nl = {};
    esp_netif_get_sta_list(&wl, &nl);
    for (int i = 0; i < nl.num && _host_n < SCAN_LOG_MAX; ++i) {
        char mac[20], ip[20];
        std::snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                      wl.sta[i].mac[0], wl.sta[i].mac[1], wl.sta[i].mac[2],
                      wl.sta[i].mac[3], wl.sta[i].mac[4], wl.sta[i].mac[5]);
        std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
                      nl.sta[i].ip.addr & 0xff,
                      (nl.sta[i].ip.addr >> 8) & 0xff,
                      (nl.sta[i].ip.addr >> 16) & 0xff,
                      (nl.sta[i].ip.addr >> 24) & 0xff);
        log_host(mac, ip);
    }
    std::snprintf(_status,sizeof(_status),"AP STA: %u", _host_n);
}

static bool do_tcp_one(uint8_t i) {
    if (i >= N_PORTS) return true;
    if (_port_n >= SCAN_PORT_LOG_MAX) return true;
    IPAddress ip; ip.fromString(_target);
    WiFiClient c; c.setTimeout(120);
    bool ok = c.connect(ip, PORTS[i]);
    char row[18]; std::snprintf(row, sizeof(row), "%-5u %s", PORTS[i], ok ? "OPEN" : "CLOSED");
    log_port(row); c.stop();
    return false;
}
#endif

// ── Lifecycle ───────────────────────────────────────────────────────────

void app_scan_init() { load_target(); _phase = 0; }

void app_scan_tick() {
    if (_phase != 2) return;
    if (_row == 0) { do_arp(); _phase = 3; }
    else if (_row == 1) {
        // Two probes per frame max so the UI stays responsive.
        for (int k = 0; k < 2 && _run_i < N_PORTS && _port_n < SCAN_PORT_LOG_MAX; ++k) {
            if (do_tcp_one(_run_i++)) break;
        }
        if (_run_i >= N_PORTS || _port_n >= SCAN_PORT_LOG_MAX) {
            std::snprintf(_status, sizeof(_status), "PORTS: %u/%u", _port_n, N_PORTS);
            _phase = 3;
        }
    } else if (_row == 3) { do_arp(); _phase = 3; }
}

void app_scan_suspend() { _phase = 0; }

// ── Event handler ───────────────────────────────────────────────────────

void app_scan_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) { _phase = 0; std::snprintf(_status,sizeof(_status),"READY"); return; }
    if (e.type == EVT_POT_CHANGED) {
        if (_row == 2) {
            // POT scrolls the table (8 rows visible).
            int v = (e.data * N_HINTS) / 256;
            if (v > (int)N_HINTS - 8) v = N_HINTS - 8;
            if (v < 0) v = 0;
            _hint_scroll = v;
            return;
        }
        _row = (e.data * N_ROWS) / 256;
        if (_row >= N_ROWS) _row = N_ROWS - 1;
        return;
    }
    if (e.type == EVT_BTN_A_DOWN) {
        if (_row == 0 || _row == 3) { _phase = 2; }
        else if (_row == 1) { _port_n = 0; _run_i = 0; _phase = 2; std::snprintf(_status,sizeof(_status),"scan.."); }
        return;
    }
}

// ── Public accessors (cross-app reach) ──────────────────────────────────

uint16_t app_scan_host_count() { return _host_n; }
const char *app_scan_host_at(uint16_t i) { return i < _host_n ? _host_log[i] : ""; }
uint16_t app_scan_port_count() { return _port_n; }
const char *app_scan_port_at(uint16_t i) { return i < _port_n ? _port_log[i] : ""; }

// ── Draw ────────────────────────────────────────────────────────────────

static void draw_list(int y, const char *title, char (*arr)[18], uint16_t n, int cols) {
    draw_textf(8, y, T_FG, T_BG, FONT_SM, "%s", title);
    int yy = y + 12;
    uint16_t shown = n > (uint16_t)(8 * cols) ? (uint16_t)(8 * cols) : n;
    for (uint16_t i = 0; i < shown; ++i)
        draw_textf(8, yy + (int)i * 11, T_DIM, T_BG, FONT_SM, "%s", arr[i]);
}

void app_scan_draw() {
    draw_fill(0,0,SCR_W,SCR_H,T_BG);
    draw_fill(0,0,SCR_W,STATS_H,T_PANEL);
    draw_hline(0,STATS_H,SCR_W,T_BORDER);
    draw_text(8, 8, "SCAN / RECON", T_FG, T_PANEL, FONT_SM);
    draw_textf(SCR_W-92, 8, T_DIM, T_PANEL, FONT_SM, "t:%s", _target);

    int row_h = 26;
    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + i * row_h;
        draw_textf(8, y, i == _row ? T_FG : T_DIM, T_BG, FONT_SM,
                   "%s %s", i == _row ? ">" : " ", LABELS[i]);
    }

    int list_y = STATS_H + 8 + N_ROWS * row_h + 4;
    if (_row == 0 || _row == 3) {
        // ARP / WIFI HOSTS — show as 2 columns of 16-char entries.
        for (uint16_t i = 0; i < _host_n && i < 16; ++i)
            draw_textf(8, list_y + (int)i * 11, T_DIM, T_BG, FONT_SM, "%s", _host_log[i]);
    } else if (_row == 1) {
        for (uint16_t i = 0; i < _port_n && i < 16; ++i)
            draw_textf(8, list_y + (int)i * 11, T_DIM, T_BG, FONT_SM, "%s", _port_log[i]);
    } else {
        // services hint table
        int yy = list_y;
        for (uint8_t i = _hint_scroll; i < N_HINTS && i < _hint_scroll + 8; ++i)
            draw_textf(8, yy + (int)(i-_hint_scroll) * 11, T_DIM, T_BG, FONT_SM,
                       "%-5u %s", HINTS[i].port, HINTS[i].name);
    }

    draw_textf(8, SCR_H - 28, (_phase == 2) ? T_WARN : T_DIM, T_BG, FONT_SM, "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12, "[A] RUN  [B] BACK  POT=row", T_DIM, T_BG, FONT_SM);
}
