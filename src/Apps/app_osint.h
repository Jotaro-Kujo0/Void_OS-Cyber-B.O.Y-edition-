// app_osint.h — OSINT gatherer. WHOIS/DNS/SUBDOM/GEO/DORK.
// Gated on whois/dig/curl presence, degrades bare-box.

#pragma once
#include "os/events.h"

void app_osint_init();
void app_osint_tick();
void app_osint_draw();
void app_osint_event(Event e);
void app_osint_suspend();