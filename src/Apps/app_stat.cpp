#include "app_stat.h"
#include "ui/draw.h"
#include "ui/theme.h"
#include "os/scheduler.h"
#include "config.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <stdio.h>

static OneWire           _ow(PIN_ONEWIRE);
static DallasTemperature _ds(&_ow);
static float  _temp_c     = 25.0f;
static float  _temp_sm    = 25.0f;
static float  _batt       = 0.87f;  // replace: adc read + voltage divider
static float  _heart      = 0.72f;  // replace: MAX30102 pulse sensor
static float  _signal     = 0.6f;   // replace: CC1101 RSSI normalized
static bool   _ds_ok      = false;

// Task registered with scheduler — runs at 1Hz
static void stat_sensor_task() {
    _temp_c  = _ds.getTempCByIndex(0);
    if (_temp_c == DEVICE_DISCONNECTED_C) _temp_c = _temp_sm;
    _ds.requestTemperatures();
    _temp_sm += (_temp_c - _temp_sm) * 0.1f;
}

void app_stat_init() {
    _ds.begin();
    _ds_ok = (_ds.getDeviceCount() > 0);
    if (_ds_ok) { _ds.setResolution(9); _ds.requestTemperatures(); }
    sched_add("stat_sens", stat_sensor_task, 1000, 5);
}

void app_stat_tick() {}   // sensors handled by scheduler task

static void draw_gauge_row(int y, const char *label,
                            float value, float raw,
                            const char *unit, uint16_t color) {
    draw_text(8, y, label, T_DIM, T_BG, FONT_SM);
    draw_bar(48, y, SCR_W-110, 10, value, color, 0x0140);
    draw_textf(SCR_W-58, y, color, T_BG, FONT_SM, "%.1f%s", raw, unit);
}

void app_stat_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);

    // Header
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_text(8, 8, "STATUS / VITALS", T_FG, T_PANEL, FONT_SM);

    // Large temperature
    draw_textf(SCR_W/2-40, 40, T_FG, T_BG, FONT_LG,
               "%.1f", _temp_sm);
    draw_text(SCR_W/2+30, 48, "C", T_DIM, T_BG, FONT_MD);
    if (!_ds_ok)
        draw_text(SCR_W/2-30, 80, "no sensor", T_WARN, T_BG, FONT_SM);

    // Gauge rows
    int y = 100;
    draw_gauge_row(y,    "TEMP", (_temp_sm)/50.0f, _temp_sm, "C",   T_FG);
    draw_gauge_row(y+24, "BAT",  _batt,           _batt*100, "%",   0x07E0);
    draw_gauge_row(y+48, "HRT",  _heart,           _heart*200,"bpm",0xFD20);
    draw_gauge_row(y+72, "SIG",  _signal,          _signal*100,"%", T_ACCENT);

    // CPU load bar
    draw_hline(8, 190, SCR_W-16, T_BORDER);
    draw_textf(8, 196, T_DIM, T_BG, FONT_SM, "CPU %d%%", sched_load());
    draw_bar(60, 196, SCR_W-80, 6, sched_load()/100.0f, T_BORDER, T_BG);

    draw_hline(0, SCR_H-16, SCR_W, T_BORDER);
    draw_text(8, SCR_H-12, "[B] BACK", T_DIM, T_BG, FONT_SM);
}