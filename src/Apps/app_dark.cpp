// app_dark.cpp — stealth/dark-mode hub.
// WiFi/BLE/RF/NET/physical + per-op MAC rotation.

#include "app_dark.h"
#include "../UI/draw.h"
#include "../UI/theme.h"
#include "../hal/hal_wifi.h"
#include "../hal/hal_mosfet.h"
#include "../hal/hal_haptic.h"
#include "../hal/hal_network.h"
#include "../hal/hal_storage.h"
#include "../hal/hal_display.h"
#include "../os/scheduler.h"
#include "../config.h"
#include <cstdio>
#include <cstring>
//  MENU


static const char *LABELS[] = {
    "WIFI (MAC/MODE)",   // 0
    "BLE  (RPA/PASS)",   // 1
    "NET  (DNS/TTL)",    // 2
    "PHYS (LED/BL/HA)",  // 3
    "OPS  (PER-OP)",     // 4 — per-op MAC rotation
};
static const uint8_t N_ROWS = sizeof(LABELS) / sizeof(LABELS[0]);


//  STATE


static bool _active[N_ROWS] = {};
static char _status[24] = "READY";

// OPS scheduled-task state
static const uint32_t OPS_INTERVAL_MS = 60000;
static int8_t  _ops_task_id = -1;
static bool    _ops_on      = false;

//  OPS STEALTH TICK (runs every 60 s via scheduler)

//
// Intentionally tiny: one MAC write + one ARP flush. No interface
// bounce, no link reset — those produce detectable radio outages.

static void ops_stealth_tick() {
    if (!_ops_on) return;
    hal_network_randomize_mac("wlan0");
    hal_network_flush_arp();
}

//  PERSISTENCE


static void load() {
    for (uint8_t i = 0; i < 4; ++i) {
        char key[16];
        std::snprintf(key, sizeof(key), "dark_%u", i);
        _active[i] = hal_storage_get_u8(key, 0) != 0;
    }
    _ops_on = hal_storage_get_u8("dark_ops", 0) != 0;
}

static void save_row(uint8_t i) {
    char key[16];
    std::snprintf(key, sizeof(key), "dark_%u", i);
    hal_storage_set_u8(key, _active[i] ? 1 : 0);
}

static void save_ops() {
    hal_storage_set_u8("dark_ops", _ops_on ? 1 : 0);
}

//  PER-ROW APPLY


static void apply_wifi(bool on) {
    if (on) {
        hal_network_randomize_mac("wlan0");
        hal_network_randomize_hostname();
        hal_network_block_all_discovery();
    } else {
        hal_network_unblock_all_discovery();
    }
    hal_wifi_set_stealth(on);
}

static void apply_ble(bool on) {
    // RF kill MOSFET is the safest "off" path on this hardware.
    hal_mosfet_set(MOSFET_RF, on ? MOSFET_OFF : MOSFET_ON);
}

static void apply_net(bool on) {
    if (on) {
        hal_network_doh_enable("https://1.1.1.1/dns-query");
        hal_network_set_ttl(64);
    } else {
        hal_network_dns_clear();
    }
}

static void apply_phys(bool on) {
    hal_display_bl_set(on ? 0 : BL_FULL);
    hal_mosfet_set(MOSFET_IR_ARRAY, on ? MOSFET_OFF : MOSFET_ON);
    if (on) hal_haptic_silence();
}

static void apply(uint8_t i, bool on) {
    switch (i) {
        case 0: apply_wifi(on);  break;
        case 1: apply_ble(on);   break;
        case 2: apply_net(on);   break;
        case 3: apply_phys(on);  break;
        // Row 4 (OPS) handled by scheduled task only.
    }
}

//  LIFECYCL

void app_dark_init() {
    load();
    // Re-apply persisted posture so a reboot doesn't desync state.
    for (uint8_t i = 0; i < 4; ++i) apply(i, _active[i]);

    // OPS registers the periodic task exactly once. Runs whether or not
    // app_dark is the current app — the scheduler ticks every frame.
    if (_ops_task_id < 0) {
        _ops_task_id = sched_add("ops_stealth", ops_stealth_tick,
                                 OPS_INTERVAL_MS, 5);
    }
    std::snprintf(_status, sizeof(_status),
                  _active[0] ? "STEALTH" : "READY");
}

void app_dark_tick()  {}
void app_dark_suspend() {}

//  EVENT HANDLER

void app_dark_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) {
        std::snprintf(_status, sizeof(_status), "READY");
        return;
    }

    if (e.type == EVT_POT_CHANGED) {
        // POT could scroll individual rows — reserved for future UI pass.
        (void)e;
        return;
    }

    if (e.type != EVT_BTN_A_DOWN) return;

    // A flips all 4 legacy categories + OPS in one press.
    for (uint8_t i = 0; i < 4; ++i) {
        _active[i] = !_active[i];
        save_row(i);
        apply(i, _active[i]);
    }
    _ops_on = !_ops_on;
    save_ops();

    std::snprintf(_status, sizeof(_status),
                  _active[0] && _ops_on ? "STEALTH+OPS" :
                  _active[0]            ? "STEALTH"     :
                  _ops_on               ? "OPS"         : "READY");
}

//  DRAW

void app_dark_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_text(8, 8, "DARK / STEALTH", T_FG, T_PANEL, FONT_SM);
    draw_textf(SCR_W - 56, 8,
               hal_wifi_is_stealth() ? T_WARN : T_DIM,
               T_PANEL, FONT_SM, "STL:%d", hal_wifi_is_stealth());
    draw_textf(SCR_W - 24, 8,
               _ops_on ? T_WARN : T_DIM,
               T_PANEL, FONT_SM, "OPS");

    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + (int)i * 26;
        bool is_on = (i == 4) ? _ops_on : _active[i];
        const char *label = is_on ? "ON " : "OFF";
        draw_textf(8, y,
                   is_on ? T_WARN : T_DIM,
                   T_BG, FONT_SM, "%s %s [%s]",
                   is_on ? ">" : " ",
                   LABELS[i], label);
    }

    draw_textf(8, SCR_H - 36,
               (_active[0] || _ops_on) ? T_WARN : T_DIM,
               T_BG, FONT_SM, "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12,
              "[A] TOGGLE ALL  [B] BACK  NOTE: OPS rotates MAC every 60s",
              T_DIM, T_BG, FONT_SM);
}
