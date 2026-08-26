// app_rogue.h — slot APP_ROGUE: rogue AP, captive portal, DNS spoof.
//
// Sub-modes:
//    0. SOFTAP   — open a soft AP with the SSID in `rogue_ssid`; channel
//                  from `rogue_chan`; show connected stations.
//    1. CAPTIVE  — start captive HTTP on port 80. Renders a generator
//                  HTML form ("Sign in to <ssid>") that POSTs email +
//                  password back to the device. Posts land in
//                  /var/lib/void-os/loot/logins.csv on Pi 5 /
//                  /sd/void-os/loot/logins.csv on ESP32.
//    2. DNS SPOOF — UDP/53 listener that returns the AP's own IP for any
//                  A query. Combined with CAPTIVE, every browser request
//                  is forced to the captive form. Cached lookups in
//                  /tmp/void-os/dnsspoof.log for forensics.
//    3. LOOT     — read-only viewer; show the last 8 captured rows.
//
// Background pumps (DNS listener, HTTP server) run as registered
// scheduled tasks while armed. They survive the operator leaving the
// app: a red teamer arms rogue, walks away to a laptop, and likely
// never touches the device screen again.

#pragma once
#include "os/events.h"

void app_rogue_init();
void app_rogue_tick();
void app_rogue_draw();
void app_rogue_event(Event e);
void app_rogue_suspend();
