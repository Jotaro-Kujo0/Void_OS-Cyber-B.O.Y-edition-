// app_dark.h — stealth/dark-mode hub.
// WIFI/BLE/NET/PHYS/OPS toggles; A flips all, OPS schedules re-roll.

#pragma once
#include "../os/events.h"

void app_dark_init();
void app_dark_tick();
void app_dark_draw();
void app_dark_event(Event e);
void app_dark_suspend();
