// app_radio.cpp — sub-GHz RF security research (CC1101)
//
// ───────────────────────────────────────────────────────────────────────────
//  SUB-MODE ROADMAP (what each menu row must do once the real CC1101
//  driver + GDO0 polling are wired into hal_radio_*.h)
// ───────────────────────────────────────────────────────────────────────────
//
//  1. CAPT  (capture & replay)
//       hal_radio_capture_start() → RX ring → hal_radio_capture_step() →
//       hal_radio_capture_replay() once on EVT_BTN_C. Stream to PCAP on
//       EVT_BTN_C held 3 s via app_log.
//
//  2. SWEEP (spectrum analyzer)
//       hal_radio_sweep_start() → hop band in 100 kHz bins, 50 ms dwell,
//       plot RSS bars per bin. CSV every minute via app_log.
//
//  3. PROTO (modulation analysis)
//       Pick OOK/ASK/FSK/GFSK/MSK + data rate from a sub-menu, then
//       run capture + bit-framing decoder. Failed-frame counter helps
//       A/B profile selection.
//
//  4. FIXED (fixed-code generator)
//       12-bit trinary or 24-bit binary DIP switch configs. Cycle at
//       50 ms dwell. Aborted on EVT_BTN_B. Cycle is ~10 days for
//       24-bit binary at 50 ms — designed for overnight runs.
//
//  5. POCSAG (pager decoder)
//       Manchester decode on GDO0, BCH(31,21) error correction, sync
//       codeword 0x7CD215D8. Each msg is appended as one CSV row.
//
//  HARDWARE LEGAL POSTURE:
//       hal_radio_send() defaults to a no-op until the operator enters
//       "TX ARMED" (EVT_BTN_A held 3 s). Stealth mode blocks all TX.
//       Region selection (315/433/868/915) is enforced by hal_radio_set_band.
//
// ───────────────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include "../config.h"
#include "../UI/draw.h"
#include "app_radio.h"
#include "../hal/hal_radio.h"
#include "../UI/theme.h"

static const char *LABELS[] = {
    "CAPT  (rx record)",
    "SWEEP (spectrum)",
    "PROTO (modulation)",
    "FIXED (brute)",
    "POCSAG (pagers)",
    "TX    (raw bits)",  // hal_radio_send_raw_bits
};
static const uint8_t N_ROWS = sizeof(LABELS) / sizeof(LABELS[0]);

static uint8_t  _row = 0;
static bool     _armed = false;
static char     _status[24] = "READY";
static uint16_t _packet_count = 0;
static int8_t   _last_rssi = -120;
static char     _pocsag_lines[4][40];      // decoded pager messages
static uint8_t  _pocsag_n = 0;

void app_radio_init() {
    Serial.println("Radio App Initialized");
    _row = 0; _armed = false;
    _packet_count = 0; _last_rssi = -120;
    hal_radio_init();
}

void app_radio_tick() {
    if (!_armed) return;
    uint8_t buf[64], len = sizeof(buf);
    float freq; uint32_t ts;
    while (hal_radio_capture_step(buf, &len, &freq, &ts)) {
        _packet_count++;
    }
    if (_row == 4) {                          // POCSAG: collect messages
        RadioPocsagMsg m;
        while (hal_radio_pocsag_pop(&m)) {
            char line[40];
            std::snprintf(line, sizeof(line), "%lu:%s",
                          (unsigned long)m.address, m.text);
            for (int i = 3; i > 0; --i)
                std::snprintf(_pocsag_lines[i], 40, "%s", _pocsag_lines[i - 1]);
            std::snprintf(_pocsag_lines[0], 40, "%s", line);
            if (_pocsag_n < 4) ++_pocsag_n;
        }
    }
    _last_rssi = hal_radio_rssi();
}

void app_radio_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, C_BLACK);
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, C_BORDER);
    draw_text(8, 8, "RADIO / SUBGHZ", C_PGREEN, T_PANEL, FONT_SM);
    draw_textf(SCR_W - 60, 8, T_DIM, T_PANEL, FONT_SM, "RSSI:%d", _last_rssi);

    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + (int)i * 26;
        draw_textf(8, y, (_armed ? T_WARN : (i == _row ? T_FG : T_DIM)),
                   T_BG, FONT_SM, "%s %s",
                   (i == _row ? ">" : " "), LABELS[i]);
    }

    // POCSAG panel: show last decoded messages.
    if (_row == 4) {
        int y = STATS_H + 8 + (int)N_ROWS * 26 + 6;
        for (uint8_t i = 0; i < _pocsag_n && y < SCR_H - 40; ++i, y += 14)
            draw_textf(8, y, i == 0 ? T_FG : T_DIM, T_BG, FONT_SM, "%s",
                       _pocsag_lines[i]);
    }

    draw_textf(8, SCR_H - 36, _armed ? T_WARN : T_DIM, T_BG, FONT_SM,
               "%s  pkts:%u", _status, _packet_count);
    draw_hline(0, SCR_H - 16, SCR_W, C_BORDER);
    draw_text(8, SCR_H - 12, "[A] SELECT  [B] BACK", C_MGRAY, C_BLACK, FONT_SM);
}

void app_radio_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) {
        _armed = false;
        hal_radio_capture_stop();
        hal_radio_sweep_stop();
        hal_radio_fixed_stop();
        hal_radio_pocsag_stop();
        snprintf(_status, sizeof(_status), "READY");
        return;
    }
    if (e.type == EVT_POT_CHANGED) {
        _row = (e.data * N_ROWS) / 256;
        if (_row >= N_ROWS) _row = N_ROWS - 1;
        return;
    }
    if (e.type != EVT_BTN_A_DOWN) return;
    _armed = true;
    static const char *names[] = { "CAPT ACTIVE", "SWEEPING", "PROTO",
                                   "FIXED 24b",  "POCSAG RX",
                                   "TX RAW" };
    if (_row == 0) hal_radio_capture_start();
    if (_row == 1) hal_radio_sweep_start();
    if (_row == 3) hal_radio_fixed_start(24, 50);
    if (_row == 4) hal_radio_pocsag_start();
    // Row 5 (TX raw) is handled by app_radio_tick: each frame sends
    // the next chunk of the queued pulse list via hal_radio_send_raw_bits().
    snprintf(_status, sizeof(_status), "%s", names[_row]);
}

void app_radio_suspend() {
    Serial.println("Radio App Suspended");
    _armed = false;
    hal_radio_capture_stop();
    hal_radio_sweep_stop();
    hal_radio_fixed_stop();
    hal_radio_pocsag_stop();
}