// app_web.h — small web scraper. URL / GET / PARSE / LINKS.
// Pi5 shells out to curl; ESP32 uses WiFiClient. Both -> loot.

#pragma once
#include "os/events.h"

void app_web_init();
void app_web_tick();
void app_web_draw();
void app_web_event(Event e);
void app_web_suspend();
