// app_rogue.h — rogue AP + captive portal + DNS spoof.
// SOFTAP / CAPTIVE / DNS SPOOF / LOOT; login posts -> loot/logins.csv.
// Pumps run as scheduled tasks, survive navigation away.

#pragma once
#include "os/events.h"

void app_rogue_init();
void app_rogue_tick();
void app_rogue_draw();
void app_rogue_event(Event e);
void app_rogue_suspend();
