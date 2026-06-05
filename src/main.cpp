#include <Arduino.h>
#include "config.h"
#include "UI/draw.h"
#include "UI/theme.h"
#include "UI/transition.h"
#include "os/events.h"
#include "os/scheduler.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_storage.h"
#include "hal/hal_power.h"
#include "Apps/app_base.h"
#include "Apps/app_home.h"
#include "Apps/app_stat.h"
#include "Apps/app_radio.h"
#include "Apps/app_nfc.h"
#include "Apps/app_ir.h"
#include "Apps/app_sys.h"
#include "UI/character/sprite.h"
#include "hal/hal_buttons_exp.h" // Handles PCF8574
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>


// ── App registry — one entry per app ─────────────────────────────────────
static const AppDef APPS[APP_COUNT] = {
    {"HOME", "",         app_home_init,  app_home_tick,  app_home_draw,  nullptr,       nullptr,         nullptr},
    {"STAT", "vitals",   app_stat_init,  app_stat_tick,  app_stat_draw,  nullptr,       nullptr,         nullptr},
    {"MAP",  "navigate", nullptr,        nullptr,        nullptr,        nullptr,       nullptr,         nullptr},
    {"LOG",  "logger",   nullptr,        nullptr,        nullptr,        nullptr,       nullptr,         nullptr},
    {"RADIO","sub-ghz",  app_radio_init, app_radio_tick, app_radio_draw, app_radio_event, app_radio_suspend, nullptr},
    {"NFC",  "rfid",     app_nfc_init,   app_nfc_tick,   app_nfc_draw,   app_nfc_event, app_nfc_suspend, nullptr},
    {"IR",   "infrared", app_ir_init,    app_ir_tick,    app_ir_draw,    app_ir_event,  app_ir_suspend,  app_ir_resume},
    {"SYS",  "system",   app_sys_init,   app_sys_tick,   app_sys_draw,   app_sys_event, nullptr,         nullptr},
};

static uint8_t _cur = APP_HOME;
static bool    _in_app = false;

// ── UI task — runs at 30fps ───────────────────────────────────────────────
static void ui_task() {
    if (transition_running()) { transition_tick(); return; }

    while (!events_empty()) {
        Event e = events_pop();
        hal_power_activity();

        // Global B = back to home
        if (_in_app && e.type == EVT_BTN_B_DOWN) {
            if (APPS[_cur].suspend) APPS[_cur].suspend();
            transition_start(TRANS_SLIDE_RIGHT);
            _cur = APP_HOME; _in_app = false;
            continue;
        }

        if (!_in_app) {
            app_home_event(e);
            if (e.type == EVT_BTN_A_DOWN) {
                uint8_t sel = app_home_selected();
                if (sel > 0 && sel < APP_COUNT && APPS[sel].draw) {
                    if (APPS[sel].init)   APPS[sel].init();
                    if (APPS[sel].resume) APPS[sel].resume();
                    transition_start(TRANS_SLIDE_LEFT);
                    _cur = sel; _in_app = true;
                }
            }
        } else {
            if (APPS[_cur].event) APPS[_cur].event(e);
        }
    }

    // Draw
    if (!_in_app)       app_home_draw();
    else if (APPS[_cur].draw) APPS[_cur].draw();
}

// ── Input task — runs every frame ────────────────────────────────────────
static void input_task() {
    hal_input_tick();
}


// ── Power task — runs every second ───────────────────────────────────────
static void power_task() {
    hal_power_tick();
}

// ── Boot splash ───────────────────────────────────────────────────────────
static void boot_splash() {
    draw_clear(T_BG);
    draw_textf(SCR_W/2-44, SCR_H/2-16, T_FG, T_BG, FONT_MD, "CYBERDECK");
    draw_textf(SCR_W/2-12, SCR_H/2+8,  T_DIM,T_BG, FONT_SM, "v2.0");
    draw_hline(20, SCR_H/2+24, SCR_W-40, T_BORDER);

    

    const char *lines[] = {
        "initializing system...",
        "Checking hardware...OK",
        "Checking storage...OK",
        "init input........OK",
        "init buttons.....OK",
        "init display.....OK",
        "init cc1101......OK",
        "init pn532.......OK",
        "init ir..........OK",
        "load storage.....OK",
        "SYSTEM READY...GO HACK SOME",
    };
    for (int i=0; i<6; i++) {
        draw_textf(20, SCR_H/2+32+i*14, i==5?T_FG:T_DIM, T_BG,
                   FONT_SM, "%s", lines[i]);
        delay(180);
    }
    delay(500);
}

void setup() {
    Serial.begin(115200);
    hal_storage_init();
    draw_init();
    events_init();
    hal_input_init();
    hal_power_init();

    boot_splash();

    sched_init();
    sched_add("input",  input_task, 0,    1);  // every frame, highest priority
    sched_add("ui",     ui_task,    0,    2);  // every frame
    sched_add("power",  power_task, 1000, 9);  // every second

    app_home_init();
    draw_clear(T_BG);
}

void loop() {
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last < FRAME_MS) return;
    last = now;
    sched_tick();
}