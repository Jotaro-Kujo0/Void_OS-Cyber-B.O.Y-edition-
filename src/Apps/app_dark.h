// app_dark.h — Stealth / dark-mode settings hub (slot APP_DARK)
//
// Sub-modes:
//   0. WIFI (MAC/MODE)  — MAC randomization, passive mode, hostname
//   1. BLE (RPA/PASS)   — RPA rotation, passive scan, RF MOSFET kill
//   2. NET (DNS/TTL)    — DoH/DoT, TTL spoofing, static IP
//   3. PHYS (LED/BL/HA) — backlight off, LED suppress, haptic only
//   4. OPS (PER-OP)     — periodic MAC re-roll + ARP flush (60s)
//
// A flips all toggles at once. OPS registers a scheduled task on init.

#pragma once
#include "../os/events.h"

void app_dark_init();
void app_dark_tick();
void app_dark_draw();
void app_dark_event(Event e);
void app_dark_suspend();
