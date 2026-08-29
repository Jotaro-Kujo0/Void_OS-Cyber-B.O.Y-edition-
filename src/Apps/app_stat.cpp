#include "app_stat.h"
#include "UI/draw.h"
#include "UI/theme.h"
#include "os/scheduler.h"
#include "hal/hal_battery.h"
#include "hal/hal_metrics.h"
#include "hal/hal_usb_msc.h"
#include "config.h"
#include <stdio.h>

#ifdef VOIDOS_RPI5
#include <filesystem>
#include <fstream>
#include <string>
#endif

static float _temp_c = 25.0f;
static float _temp_sm = 25.0f;
static float _batt = 0.87f;
static float _heart = 0.72f;
static float _signal = 0.6f;
static bool _ds_ok = false;

#ifdef VOIDOS_RPI5
static bool read_pi_temperature(float &value) {
    const std::string root = "/sys/class/thermal/thermal_zone0/temp";
    std::ifstream in(root);
    int millidegrees = 0;
    if (!in || !(in >> millidegrees)) return false;
    value = millidegrees / 1000.0f;
    return true;
}
static void stat_sensor_task() {
    float sample;
    _ds_ok = read_pi_temperature(sample);
    if (_ds_ok) { _temp_c = sample; _temp_sm += (_temp_c - _temp_sm) * 0.1f; }
    _batt = hal_battery_soc() / 100.0f;
}
#else
#include <OneWire.h>
#include <DallasTemperature.h>
static OneWire _ow(PIN_ONEWIRE);
static DallasTemperature _ds(&_ow);
static void stat_sensor_task() { _temp_c=_ds.getTempCByIndex(0); if(_temp_c==DEVICE_DISCONNECTED_C)_temp_c=_temp_sm; _ds.requestTemperatures(); _temp_sm+=(_temp_c-_temp_sm)*0.1f; }
#endif

void app_stat_init() {
#ifdef VOIDOS_RPI5
    hal_battery_init();
    _ds_ok = false;
#else
    _ds.begin(); _ds_ok=(_ds.getDeviceCount()>0); if(_ds_ok){_ds.setResolution(9);_ds.requestTemperatures();}
#endif
    sched_add("stat_sens", stat_sensor_task, 1000, 5);
}
void app_stat_tick() {}

static void draw_gauge_row(int y, const char *label, float value, float raw, const char *unit, uint16_t color) {
    draw_text(8,y,label,T_DIM,T_BG,FONT_SM); draw_bar(48,y,SCR_W-110,10,value,color,0x0140); draw_textf(SCR_W-58,y,color,T_BG,FONT_SM,"%.1f%s",raw,unit);
}

void app_stat_draw() {
    draw_fill(0,0,SCR_W,SCR_H,T_BG); draw_fill(0,0,SCR_W,STATS_H,T_PANEL); draw_hline(0,STATS_H,SCR_W,T_BORDER); draw_text(8,8,"STATUS / VITALS",T_FG,T_PANEL,FONT_SM);

    SystemUsage m = hal_metrics_snapshot();

    draw_textf(SCR_W/2-40,40,T_FG,T_BG,FONT_LG,"%.1f",_temp_sm); draw_text(SCR_W/2+30,48,"C",T_DIM,T_BG,FONT_MD);
    if (!_ds_ok) draw_text(SCR_W/2-30,80,"no sensor",T_WARN,T_BG,FONT_SM);
    int y=100;
    draw_gauge_row(y,"TEMP", _temp_sm/50.0f, _temp_sm,"C",   T_FG);
    draw_gauge_row(y+24,"BAT",  _batt,         _batt*100,"%", 0x07E0);
    draw_gauge_row(y+48,"HRT",  _heart,        _heart*200,"bpm",0xFD20);
    draw_gauge_row(y+72,"SIG",  _signal,       _signal*100,"%", T_ACCENT);

    // System usage column — read more than per-sensor values.
    draw_hline(8, 196, SCR_W-16, T_BORDER);
    int sy = 204;
    draw_textf(8, sy, m.cpu_pct > 85 ? T_ERR : (m.cpu_pct > 60 ? T_WARN : T_FG),
               T_BG, FONT_SM, "CPU %u%%",  m.cpu_pct);
    draw_bar(SCR_W-100, sy, 88, 8, m.cpu_pct/100.0f,
             m.cpu_pct > 85 ? T_ERR : (m.cpu_pct > 60 ? T_WARN : T_FG), T_BG);
    sy += 14;
    draw_textf(8, sy, m.mem_pct > 90 ? T_ERR : (m.mem_pct > 70 ? T_WARN : T_DIM),
               T_BG, FONT_SM, "MEM %u%% (%u kB free)", m.mem_pct, m.mem_free_kb);
    sy += 14;
    draw_textf(8, sy, T_DIM, T_BG, FONT_SM, "SD  %u%%  rx %ukB  tx %ukB",
               m.sdc_pct, m.net_rx_total_kb, m.net_tx_total_kb);
    sy += 14;
    draw_textf(8, sy, T_DIM, T_BG, FONT_SM,
               "up %u:%02u:%02u  load %u.%02u", m.uptime_s/3600, (m.uptime_s/60)%60, m.uptime_s%60,
               m.load_avg_1m_x100/100, m.load_avg_1m_x100%100);
    sy += 14;
    {
        UsbMscStats usb = hal_usb_msc_stats();
        uint16_t uc = usb.active ? T_FG : T_DIM;
        if (usb.active)
            draw_textf(8, sy, uc, T_BG, FONT_SM, "USB ON %u file%s  %u kB free",
                       usb.files_count, usb.files_count == 1 ? "" : "s",
                       (unsigned)usb.free_kb);
        else
            draw_textf(8, sy, uc, T_BG, FONT_SM, "USB OFF");
    }

    WatchdogState wd = hal_metrics_watchdog_snapshot();
    if (!wd.ok) {
        draw_textf(8, SCR_H - 32, T_ERR, T_BG, FONT_SM, "WDOG: stalled tag \"%s\"",
                   wd.tag_name[wd.stalled_tag]);
    }

    draw_hline(0,SCR_H-16,SCR_W,T_BORDER); draw_text(8,SCR_H-12,"[B] BACK",T_DIM,T_BG,FONT_SM);
}
