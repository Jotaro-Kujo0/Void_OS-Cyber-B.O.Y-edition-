// app_wifi.cpp — flat menu over hal_wifi/hal_ble sub-modes.

#include "app_wifi.h"
#include "UI/draw.h"
#include "UI/theme.h"
#include "hal/hal_wifi.h"
#include "config.h"
#include <cstdio>

static const char *LABELS[] = {
    "SCAN  (WARD)",     // 0 — wardriving + GPS tagging
    "CAPT  (MGMT)",     // 1 — 802.11 monitor + deauth test
    "HSHK  (EAPOL)",    // 2 — 4-way handshake + PMKID capture
    "ROGUE (AP)",       // 3 — soft-AP, probe-request harvesting
    "KARMA (PROBE)",    // 4 — respond to every probe request
    "INJCT (FRAME)",    // 5 — raw 802.11 frame injection
    "BEACN (SPOOF)",    // 6 — custom BLE advertisement spoof
    "BLE   (PASSIVE)",  // 7 — passive BLE adv scan + GATT enum
    "PMKID (RSN)",      // 8 — RSN IE PMKID extract (no deauth needed)
    "WPS   (REAP)",     // 9 — WPS PIN attack (online/reaper)
};
static const uint8_t N_ROWS = sizeof(LABELS) / sizeof(LABELS[0]);

// ponytail: rows 8–9 (PMKID/WPS) are stubs; vendor+pixiewps if needed.

static uint8_t _row = 0;
static bool    _active = false;
static char    _status[24] = "READY";

void app_wifi_init()  { _row = 0; _active = false; snprintf(_status, sizeof(_status), "READY"); }
void app_wifi_tick()  {}
void app_wifi_suspend() { _active = false; }

void app_wifi_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) { _active = false; snprintf(_status, sizeof(_status), "READY"); return; }
    if (e.type == EVT_POT_CHANGED) { _row = (e.data * N_ROWS) / 256; if (_row >= N_ROWS) _row = N_ROWS - 1; return; }
    if (e.type != EVT_BTN_A_DOWN) return;

    // KARMA / INJCT / BEACN refuse in stealth mode. The skeleton returns
    // false but the status string lets the operator see why.
    const bool stealth = hal_wifi_is_stealth();
    static const char *names[] = {
        "WARD SCAN",     "MGMT CAPT",  "EAPOL HSHK", "ROGUE AP",
        "KARMA ON",      "INJECT ON",  "BEACN SPOOF","BLE PASSIVE",
    };
    if ((_row == 4 || _row == 5 || _row == 6) && stealth) {
        snprintf(_status, sizeof(_status), "STL BLOCKS %s", names[_row]);
        return;
    }

    _active = true;
    switch (_row) {
        case 4: hal_wifi_karma_start();         break;
        case 5: (void)0;                        break;   // hal_wifi_inject_frame called per-frame from app_wifi_tick
        case 6: (void)0;                        break;   // hal_ble_advertise_custom called per-frame from app_wifi_tick
        default: break;
    }
    snprintf(_status, sizeof(_status), "%s", names[_row]);
}

void app_wifi_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_text(8, 8, "WIFI/BLE", T_FG, T_PANEL, FONT_SM);
    draw_textf(SCR_W - 56, 8, T_DIM, T_PANEL, FONT_SM, "CAPS:%02X",
               hal_wifi_capabilities());

    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + (int)i * 26;
        draw_textf(8, y, (_active ? T_WARN : (i == _row ? T_FG : T_DIM)),
                   T_BG, FONT_SM, "%s %s",
                   (i == _row ? ">" : " "), LABELS[i]);
    }

    draw_textf(8, SCR_H - 36, _active ? T_WARN : T_DIM, T_BG, FONT_SM,
               "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12, "[A] SELECT  [B] BACK", T_DIM, T_BG, FONT_SM);
}