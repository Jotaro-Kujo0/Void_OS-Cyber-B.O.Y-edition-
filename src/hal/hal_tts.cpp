// hal_tts.cpp — TTS SKELETON.
//
// =====================================================================
//  IMPLEMENTATION GUIDE
// =====================================================================
//
//  ── Pi 5 path ──────────────────────────────────────────────────────
//      * `sudo apt install espeak-ng`.
//      * `hal_tts_say("hello")` => `std::system("espeak-ng 'hello' &")`.
//      * Output to the on-board 3.5 mm jack or USB audio device.
//
//  ── ESP32 path ──────────────────────────────────────────────────────
//      * I2S DAC (e.g. PCM5102A) or UDA1334 on I2S0.
//      * ESP32-tts library (https://github.com/earlephilhower/ESP32-
//        tts) — single-header.
//      * RTOS task: queue TTS jobs; called from a pump.
//
//  =====================================================================

#include "hal_tts.h"
#ifdef VOIDOS_RPI5
#include <cstdlib>
#include <cstdio>
#endif

void hal_tts_say(const char *text) {
    if (!text) return;
#ifdef VOIDOS_RPI5
    // Use espeak-ng with quoting. Real path: wrap in a fork.
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd), "espeak-ng '%s' 2>/dev/null &", text);
    std::system(cmd);
#else
    (void)text;
#endif
}
