// app_body.cpp — passive probe capture. tcpdump pipe -> ring buffer.

#include "app_body.h"
#include "../UI/draw.h"
#include "../UI/theme.h"
#include "../hal/hal_storage.h"
#include "../hal/hal_metrics.h"
#include "../os/scheduler.h"
#include "../config.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>

#ifdef VOIDOS_RPI5
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include "../hal/hal_probe.h"
#endif

#ifdef VOIDOS_RPI5

// Shared probe ring now lives in hal_probe; app_body adds the OUI vendor
// lookup on top. BodyEntry is a thin read-only view over ProbeDevice.
struct BodyEntry {
    const uint8_t  *mac;
    uint32_t count;
    char     ssid[33];
};

static bool      _sniff = false;
static char      _status[24] = "READY";
static int8_t    _pump_id = -1;

//OUI Vendor Table 
//wallahi Im cooked
struct VendorEntry { uint32_t oui; const char *name; };

static const VendorEntry VENDORS[] = {
    { 0x000000, "Unspecified"},
    { 0x001A11, "Apple"},     { 0x002241, "Apple"},     { 0x041E64, "Apple"},
    { 0x1430A1, "Apple"},     { 0x18AF61, "Apple"},     { 0x245E48, "Apple"},
    { 0x3C0754, "Apple"},     { 0xA4B197, "Apple"},     { 0x60FB42, "Apple"},
    { 0x40CBC0, "Apple"},     { 0xB0CA68, "Apple"},     { 0x94E96A, "Apple"},
    { 0x48D682, "Apple"},     { 0xDC56E7, "Apple"},     { 0x70C9C6, "Apple"},
    { 0x8866A5, "Apple"},
    { 0x3C5AB4, "Google"},    { 0xF4F5D8, "Google"},    { 0xF8FF0B, "Google"},
    { 0x70B3D5, "Cisco"},     { 0x00163E, "Cisco"},     { 0xB07FB9, "Cisco"},
    { 0x6C5A34, "Cisco"},
    { 0xD8B377, "Samsung"},   { 0xFCAA14, "Samsung"},   { 0x9800C4, "Samsung"},
    { 0xB8C6BB, "Samsung"},
    { 0x001E58, "D-Link"},
    { 0x002401, "Xiaomi"},    { 0x146180, "Xiaomi"},
    { 0xACE069, "Huawei"},    { 0x1062EB, "Huawei"},    { 0x80B686, "Huawei"},
    { 0x0025B3, "Microsoft"}, { 0x60D248, "Microsoft"}, { 0xF8CFC2, "Microsoft"},
    { 0xE4F0D8, "Microsoft"}, { 0x0050F2, "Microsoft"},
    { 0x001D0E, "Sony"},      { 0xFC0FE6, "Sony"},
    { 0xCC36CC, "TP-Link"},   { 0xE4D332, "TP-Link"},   { 0xDCA632, "TP-Link"},
    { 0xAC84C9, "TP-Link"},   { 0xB0487A, "TP-Link"},
    { 0x001EBF, "Intel"},     { 0x7CB0C2, "Intel"},     { 0x685D43, "Intel"},
    { 0x8086F2, "Intel"},
    { 0xF0F8F5, "Hisense"},
    { 0x244C7B, "Espressif"}, { 0xAC67B2, "Espressif"},
};
static const uint8_t N_VENDORS = sizeof(VENDORS) / sizeof(VENDORS[0]);

static const char *vendor_for(const uint8_t *mac) {
    uint32_t key = ((uint32_t)mac[0] << 16) | ((uint32_t)mac[1] << 8) | mac[2];
    for (uint8_t i = 0; i < N_VENDORS; ++i) {
        if (VENDORS[i].oui == key) return VENDORS[i].name;
    }
    return "?";
}

// Ring Buff — delegated to shared hal_probe. This app registers as the
// sink so hal_probe still routes every decoded probe through
// app_body_add_packet (kept as the public ingest API for app_home).

void app_body_add_packet(const uint8_t *mac, const char *ssid) {
    // hal_probe already ingested into its ring; this hook is preserved so
    // the OUI table / cross-app API stays stable. No-op on ESP32 stub.
    (void)mac; (void)ssid;
}

static void probe_sink_cb(const uint8_t *mac, const char *ssid) {
    app_body_add_packet(mac, ssid);
}

static void sniff_on() {
    if (hal_probe_running()) { _sniff = true; return; }
    if (hal_probe_capture_start()) {
        _sniff = true;
        snprintf(_status, sizeof(_status), "SNIFF ON");
    } else {
        snprintf(_status, sizeof(_status), "no tcpdump/mon0");
    }
}

static void sniff_off() {
    hal_probe_capture_stop();
    _sniff = false;
    snprintf(_status, sizeof(_status), "SNIFF OFF");
}

static void pipe_pump() {
    hal_probe_tick();
    if (_sniff && !hal_probe_running())
        snprintf(_status, sizeof(_status), "tcpdump died");
}

// Lifecycle 

void app_body_init() {
    hal_probe_init();
    hal_probe_register_sink(probe_sink_cb);
    hal_probe_clear();
    if (_pump_id < 0) _pump_id = sched_add("body_pump", pipe_pump, 200, 6);
}

void app_body_tick() { /* pump task does work */ }

void app_body_suspend() {
    sniff_off();
    hal_probe_register_sink(nullptr);
    hal_probe_init();
}

//Event Handle

void app_body_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) {
        sniff_off();
        return;
    }
    if (e.type == EVT_BTN_A_DOWN) {
        if (_sniff) sniff_off();
        else sniff_on();
        return;
    }
    if (e.type == EVT_BTN_C_DOWN) {
        hal_probe_clear();
        snprintf(_status, sizeof(_status), "ring wiped");
        return;
    }
}

// Public Accessor

uint16_t app_body_visible_count() { return hal_probe_visible_count(); }

//Draw

struct RowCursor { uint16_t i; uint32_t count; };

static int rowcmp_count_desc(const void *a, const void *b) {
    uint32_t ca = ((const RowCursor*)a)->count;
    uint32_t cb = ((const RowCursor*)b)->count;
    if (cb > ca) return 1;
    if (cb < ca) return -1;
    return 0;
}

void app_body_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_text(8, 8, "BODY / PRESENCE", T_FG, T_PANEL, FONT_SM);
    draw_textf(SCR_W - 60, 8, _sniff ? T_WARN : T_DIM, T_PANEL, FONT_SM,
               "ON:%d", _sniff);

    // Build the sort rows from the shared hal_probe ring.
    RowCursor rows[PROBE_RING_MAX] = {};
    uint16_t n = 0;
    uint16_t rn = hal_probe_visible_count();
    for (uint16_t i = 0; i < rn && i < PROBE_RING_MAX; ++i) {
        const ProbeDevice *d = hal_probe_at(i);
        if (d) rows[n++] = { i, d->count };
    }
    qsort(rows, n, sizeof(RowCursor), rowcmp_count_desc);

    int y = STATS_H + 6;
    uint16_t shown = n > 12 ? 12 : n;
    for (uint16_t i = 0; i < shown; ++i) {
        const ProbeDevice *e = hal_probe_at(rows[i].i);
        if (!e) continue;
        char macs[20];
        snprintf(macs, sizeof(macs), "%02x:%02x:%02x:%02x:%02x:%02x",
                 e->mac[0], e->mac[1], e->mac[2],
                 e->mac[3], e->mac[4], e->mac[5]);
        draw_textf(8, y + (int)i * 11, i == 0 ? T_FG : T_DIM, T_BG, FONT_SM,
                   "%s %3u  %s  %s",
                   macs, e->count, vendor_for(e->mac),
                   e->ssid[0] ? e->ssid : "-");
    }

    int info_y = STATS_H + 6 + shown * 11 + 4;
    SystemUsage m = hal_metrics_snapshot();
    draw_textf(8, info_y, T_FG, T_BG, FONT_SM,
               "CPU %u%%  MEM %u%%  RX %ukB",
               m.cpu_pct, m.mem_pct, m.net_rx_total_kb);

    draw_textf(8, SCR_H - 28, T_DIM, T_BG, FONT_SM,
               "%s  ring:%u/%u", _status, rn, PROBE_RING_MAX);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12, "[A] ARM  [B] BACK  C=clear",
              T_DIM, T_BG, FONT_SM);
}

#else  // ESP32 — body tracking needs tcpdump (Linux)
static char _stub_status[24] = "BODY: LINUX ONLY";

void app_body_init() {
    std::snprintf(_stub_status, sizeof(_stub_status), "BODY: LINUX ONLY");
}
void app_body_tick() {}
void app_body_suspend() {}
void app_body_event(Event e) { (void)e; }
void app_body_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_text(8, 8, "BODY / PRESENCE", T_FG, T_BG, FONT_SM);
    draw_text(8, 44, _stub_status, T_WARN, T_BG, FONT_SM);
    draw_text(8, 62, "Needs tcpdump (Pi 5)", T_DIM, T_BG, FONT_SM);
}
#endif  // VOIDOS_RPI5
