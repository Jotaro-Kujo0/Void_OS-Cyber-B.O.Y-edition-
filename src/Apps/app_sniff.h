// app_sniff.h — HTTP URL/cookie sniffer + MITM toggle.
// URL LOG / COOKIES / MITM EN -> loot .csv; listener survives nav.

#pragma once
#include "os/events.h"

void app_sniff_init();
void app_sniff_tick();
void app_sniff_draw();
void app_sniff_event(Event e);
void app_sniff_suspend();
