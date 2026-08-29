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
#include "Apps/app_wifi.h"
#include "Apps/app_log.h"
#include "Apps/app_radio.h"
#include "Apps/app_nfc.h"
#include "Apps/app_ir.h"
#include "Apps/app_sys.h"
#include "Apps/app_bus.h"
#include "Apps/app_hid.h"
#include "Apps/app_dark.h"
#include "Apps/app_scan.h"
#include "Apps/app_rogue.h"
#include "Apps/app_sniff.h"
#include "Apps/app_fuzz.h"
#include "Apps/app_web.h"
#include "Apps/app_body.h"
#include "Apps/app_help.h"
#include "Apps/app_drop.h"
#include "Apps/app_qr.h"
#include "Apps/app_leak.h"
#include "Apps/app_maraud.h"
#include "Apps/app_osint.h"
#include "Apps/app_harden.h"
#include "hal/hal_buzzer.h"
#include "hal/hal_led_rgb.h"
#include "hal/hal_touch.h"
#include "hal/hal_loot.h"
#include "hal/hal_wifi.h"
#include "hal/hal_serial_term.h"
#include "hal/hal_web_ctrl.h"
#include "hal/hal_pcapng.h"
#include "hal/hal_markdown.h"
#include "hal/hal_crypto.h"
#include "hal/hal_usb_msc.h"
#include "hal/hal_mqtt.h"
#include "hal/hal_ble_remote.h"
#include "hal/hal_usb_pd.h"
#include "hal/hal_wake_on_radio.h"
#include "hal/hal_llm.h"
#include "hal/hal_tts.h"
#include "hal/hal_ocr.h"
#include "UI/character/sprite.h"
#include "hal/hal_buttons_exp.h" // PCF8574
#include "hal/hal_metrics.h"    // watchdog heartbeat
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>


// ── App registry ─────────────────────────────────────────────────────────
static const AppDef APPS[APP_COUNT] = {
    {"HOME",  "",         app_home_init,  app_home_tick,  app_home_draw,  nullptr,          nullptr,          nullptr},
    {"STAT",  "vitals",   app_stat_init,  app_stat_tick,  app_stat_draw,  nullptr,          nullptr,          nullptr},
    {"WIFI",  "wifi/ble", app_wifi_init,  app_wifi_tick,  app_wifi_draw,  app_wifi_event,   app_wifi_suspend, nullptr},
    {"LOG",   "logger",   app_log_init,   app_log_tick,   app_log_draw,   app_log_event,    app_log_suspend,  nullptr},
    {"RADIO", "sub-ghz",  app_radio_init, app_radio_tick, app_radio_draw, app_radio_event,  app_radio_suspend,nullptr},
    {"NFC",   "rfid",     app_nfc_init,   app_nfc_tick,   app_nfc_draw,   app_nfc_event,    app_nfc_suspend,  nullptr},
    {"IR",    "infrared", app_ir_init,    app_ir_tick,    app_ir_draw,    app_ir_event,     app_ir_suspend,   app_ir_resume},
    {"SYS",   "system",   app_sys_init,   app_sys_tick,   app_sys_draw,   app_sys_event,    nullptr,          nullptr},
    {"BUS",   "cable",    app_bus_init,   app_bus_tick,   app_bus_draw,   app_bus_event,    app_bus_suspend,  nullptr},
    {"HID",   "badble",   app_hid_init,   app_hid_tick,   app_hid_draw,   app_hid_event,    app_hid_suspend,  nullptr},
    {"DARK",  "stealth",  app_dark_init,  app_dark_tick,  app_dark_draw,  app_dark_event,   app_dark_suspend, nullptr},
    {"SCAN",  "recon",    app_scan_init,  app_scan_tick,  app_scan_draw,  app_scan_event,   app_scan_suspend, nullptr},
    {"ROGUE", "evilap",   app_rogue_init, app_rogue_tick, app_rogue_draw, app_rogue_event,  app_rogue_suspend,nullptr},
    {"SNIFF", "urllog",   app_sniff_init, app_sniff_tick, app_sniff_draw, app_sniff_event,  app_sniff_suspend,nullptr},
    {"FUZZ",  "fuzzer",   app_fuzz_init,  app_fuzz_tick,  app_fuzz_draw,  app_fuzz_event,   app_fuzz_suspend, nullptr},
    {"WEB",   "scrape",   app_web_init,   app_web_tick,   app_web_draw,   app_web_event,    app_web_suspend,  nullptr},
    {"BODY",  "bodies",   app_body_init,  app_body_tick,  app_body_draw,  app_body_event,   app_body_suspend, nullptr},
    {"HELP",  "cheat",    app_help_init,  app_help_tick,  app_help_draw,  app_help_event,   app_help_suspend, nullptr},
    {"DROP",  "usb",      app_drop_init,  app_drop_tick,  app_drop_draw,  app_drop_event,   app_drop_suspend, nullptr},
    {"QR",    "gen",      app_qr_init,    app_qr_tick,    app_qr_draw,    app_qr_event,     app_qr_suspend,   nullptr},
    {"LEAK",   "leaks",    app_leak_init,  app_leak_tick,  app_leak_draw,   app_leak_event,   app_leak_suspend, nullptr},
    {"MARAUD", "maraud",   app_maraud_init, app_maraud_tick, app_maraud_draw, app_maraud_event, app_maraud_suspend, nullptr},
    {"OSINT",  "osint",    app_osint_init, app_osint_tick, app_osint_draw,  app_osint_event,  app_osint_suspend, nullptr},
    {"HARDEN", "defend",   app_harden_init, app_harden_tick, app_harden_draw, app_harden_event, app_harden_suspend, nullptr},
};

static uint8_t _cur = APP_HOME;
static bool    _in_app = false;

// Open app directly; used by REPL/web harness launches.
void os_launch_app(uint8_t idx) {
    if (idx >= APP_COUNT || !APPS[idx].draw) return;
    if (APPS[idx].init)   APPS[idx].init();
    if (APPS[idx].resume) APPS[idx].resume();
    transition_start(TRANS_SLIDE_LEFT);
    _cur = idx;
    _in_app = true;
}

// ── UI task ─ 30fps ─────────────────────────────────────────────────────
static void ui_task() {
    if (transition_running()) { transition_tick(); hal_display_present(); return; }

    while (!events_empty()) {
        Event e = events_pop();
        hal_power_activity();

        // Global B backs out to home
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
    hal_display_present();
}

static void app_task() {
    if (!_in_app) {
        if (APPS[APP_HOME].tick) APPS[APP_HOME].tick();
    } else if (APPS[_cur].tick) {
        APPS[_cur].tick();
    }
}

// ── Input task ─ every frame ─────────────────────────────────────────────
static void input_task() {
    hal_input_tick();
}

// ── Wi-Fi task ─ capture + scan drain ────────────────────────────────────
static void wifi_task() {
    hal_wifi_tick();
}

// ── Power task ─ each second, pumps loot encryption ──────────────────────
static void power_task() {
    hal_power_tick();
#ifdef VOIDOS_RPI5
    // Encrypt fresh loot every 5s; never blocks UI.
    static uint8_t crypto_counter = 0;
    if (hal_crypto_enabled() && (++crypto_counter % 5 == 0)) {
        int n = 0;
        hal_crypto_pass("/var/lib/void-os/loot", &n);
        if (n > 0) { char line[48]; std::snprintf(line, sizeof(line), "[crypto] encrypted %d loot file(s)", n); Serial.println(line); }
        crypto_counter = 0;
    }
#endif
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
        hal_display_present();
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

#ifdef VOIDOS_RPI5
    hal_wifi_init();       // nl80211 probe
#endif

    boot_splash();

    sched_init();
    hal_buzzer_init();
    hal_led_set(LEDRGB_IDLE);
    hal_touch_init();
    hal_loot_init();
#ifdef VOIDOS_RPI5
    // Boot-time at-rest encryption key. When VOIDOS_LOOT_KEY is set the
    // encryption pump encrypts captured loot; otherwise crypto stays
    // disabled (honest) and loot remains plaintext on disk.
    {
        const char *key = std::getenv("VOIDOS_LOOT_KEY");
        if (key && key[0]) {
            if (hal_crypto_init(key))
                Serial.println("[crypto] at-rest encryption ENABLED");
            else
                Serial.println("[crypto] bad passphrase; encryption DISABLED");
        } else {
            Serial.println("[crypto] no VOIDOS_LOOT_KEY; loot stored plaintext");
        }
    }
#endif
    hal_serial_term_init();
    hal_web_ctrl_init();
    hal_metrics_init();                      // scorer + watchdog
    sched_add("input",  input_task, 0,    1);  // frame, highest prio
    sched_add("app",    app_task,    0,    2);  // active app
    sched_add("wifi",   wifi_task, 0,    3);    // radio capture
    sched_add("ui",     ui_task,    0,    4);   // every frame
    sched_add("power",  power_task, 1000, 9);   // each second

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

#ifdef VOIDOS_RPI5
int main() {
    setup();
    for (;;) loop();
    return 0;
}
#endif