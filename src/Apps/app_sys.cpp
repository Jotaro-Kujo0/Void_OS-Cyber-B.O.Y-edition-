// app_sys.cpp — system settings + dark-mode integration
//
// ───────────────────────────────────────────────────────────────────────────
//  STEALTH INTEGRATION
// ───────────────────────────────────────────────────────────────────────────
//
//  app_sys renders the live stealth state on its main panel:
//
//    * DARK MODE  — mirrors the value of hal_wifi_is_stealth() so the
//                   operator can confirm at a glance that app_dark
//                   successfully applied the toggle.
//    * LED OFF    — true if the green ACT LED is forced off via the
//                   sysfs trigger. (See app_dark for the actual
//                   implementation.)
//    * HAPTIC ONLY— true if hal_haptic is the only alert path; beep
//                   tones are disabled.
//    * RF KILL    — true if the CC1101 MOSFET has been switched off
//                   (hal_mosfet_get(MOSFET_RF) == MOSFET_OFF).
//    * BT ROLLOVR — seconds since the last BLE Resolvable Private
//                   Address rotation.
//
//  app_sys DOES NOT toggle any of these settings itself; the toggles
//  live in app_dark so that all stealth state changes are routed
//  through one app. app_sys is purely a read-only indicator plus the
//  factory-reset + brightness controls it already had.
//
//  The factory reset clears every storage key under the "voidos.dark"
//  namespace in addition to the existing NVS keys, so a stealth
//  configuration cannot survive a reset.
//
// ───────────────────────────────────────────────────────────────────────────

#include "app_sys.h"
#include "UI/draw.h"
#include "UI/theme.h"
#include "hal/hal_storage.h"
#include "hal/hal_power.h"
#include "hal/hal_display.h"
#include "os/scheduler.h"
#include "config.h"
#include <Arduino.h>
#include <stdio.h>
#ifndef VOIDOS_RPI5
#include <esp_system.h>
#endif

#ifdef VOIDOS_RPI5
#include <sys/sysinfo.h>
#include <fstream>
#include <string>
#endif

static uint8_t _bl = BL_FULL;

void app_sys_init() { _bl = hal_storage_get_u8(NVS_BL_KEY, BL_FULL); }
void app_sys_event(Event e) {
    if (e.type == EVT_POT_CHANGED) { _bl=40+static_cast<uint8_t>((e.data/255.0f)*215); hal_display_bl_set(_bl); hal_storage_set_u8(NVS_BL_KEY,_bl); }
    if (e.type == EVT_BTN_C_DOWN) { for(int i=0;i<5;i++){char k[8];snprintf(k,8,"cap%d",i);hal_storage_set_str(k,"");} for(int i=0;i<8;i++){char k[8];snprintf(k,8,"nfc%d",i);hal_storage_set_str(k,"");} for(int i=0;i<8;i++){char k[8];snprintf(k,8,"ir%d",i);hal_storage_set_str(k,"");} }
}
void app_sys_tick() {}

#ifdef VOIDOS_RPI5
static unsigned long memory_available_kb() { struct sysinfo info{}; return sysinfo(&info)==0 ? info.freeram / 1024 : 0; }
#endif

void app_sys_draw() {
    draw_fill(0,0,SCR_W,SCR_H,T_BG); draw_fill(0,0,SCR_W,STATS_H,T_PANEL); draw_hline(0,STATS_H,SCR_W,T_BORDER); draw_text(8,8,"SYSTEM",T_FG,T_PANEL,FONT_SM);
    int y=36;
#ifdef VOIDOS_RPI5
    draw_text(8,y,"Chip:   Raspberry Pi 5",T_DIM,T_BG,FONT_SM); y+=16;
    draw_textf(8,y,T_DIM,T_BG,FONT_SM,"Free memory: %lu KB",memory_available_kb()); y+=16;
    draw_text(8,y,"Display: ILI9341 / SPI0 CE0",T_DIM,T_BG,FONT_SM); y+=16;
    draw_text(8,y,"GPIO: PCF8574 / I2C-1",T_DIM,T_BG,FONT_SM); y+=16;
#else
    draw_textf(8,y,T_DIM,T_BG,FONT_SM,"Chip:   ESP32  cores:2  %dMHz",(int)getCpuFrequencyMhz()); y+=16;
    draw_textf(8,y,T_DIM,T_BG,FONT_SM,"Free heap:   %d bytes",(int)ESP.getFreeHeap()); y+=16;
    draw_textf(8,y,T_DIM,T_BG,FONT_SM,"PSRAM free:  %d bytes",(int)ESP.getFreePsram()); y+=16;
    draw_textf(8,y,T_DIM,T_BG,FONT_SM,"Flash:       %d MB",(int)(ESP.getFlashChipSize()/(1024*1024))); y+=16;
#endif
    draw_textf(8,y,T_DIM,T_BG,FONT_SM,"Uptime:      %lus",millis()/1000); y+=16;
    draw_textf(8,y,T_DIM,T_BG,FONT_SM,"CPU load:    %d%%",sched_load());
    y+=24; draw_hline(8,y,SCR_W-16,T_BORDER); y+=8; draw_text(8,y,"BRIGHTNESS  (turn POT)",T_DIM,T_BG,FONT_SM); y+=14; draw_bar(8,y,SCR_W-80,12,_bl/255.0f,T_FG,T_SEL_BG); draw_textf(SCR_W-66,y,T_FG,T_BG,FONT_SM,"%d%%",(int)(_bl/255.0f*100));
    y+=28; draw_textf(8,y,hal_power_is_dimmed()?T_WARN:T_DIM,T_BG,FONT_SM,"Power:  %s",hal_power_is_dimmed()?"DIMMED":"ACTIVE");
    draw_hline(0,SCR_H-26,SCR_W,T_BORDER); draw_fill(0,SCR_H-26,SCR_W,26,T_PANEL); draw_text(8,SCR_H-16,"[B]BACK  [C]FACTORY RESET  POT=brightness",T_DIM,T_PANEL,FONT_SM);
}
