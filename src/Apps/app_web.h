// app_web.h — slot APP_WEB: small HTTP web scraper.
//
// Sub-modes:
//   0. URL   — pre-saved target URL in hal_storage["web_url"]. The
//              POT adjusts a cursor over the URL so the operator can
//              drop a char with KEY A or jump to "HTTP://" / "HTTPS://".
//   1. GET   — fetch URL via background pump. Saves HTML to
//              /var/lib/void-os/loot/web_<UTC>.html (Pi 5) or
//              /sd/loot/web_<UTC>.html (ESP32).
//   2. PARSE — quick HTML peek: extract <title>, <h1>, count <a>,
//              count <form>. Rendered on screen.
//   3. LINKS — list first 8 anchor links found in the last GET.
//
// Pi 5 path uses `curl` via shell-out (hermetic build). ESP32 path uses
// `WiFiClient` + raw HTTP/1.0. Both write to the same loot path.

#pragma once
#include "os/events.h"

void app_web_init();
void app_web_tick();
void app_web_draw();
void app_web_event(Event e);
void app_web_suspend();
