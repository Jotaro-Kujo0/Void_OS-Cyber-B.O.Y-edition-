// app_sniff.h — slot APP_SNIFF: HTTP URL/cookie sniffer + MITM toggle.
//
// Sub-modes:
//    0. URL LOG  — start a listeners that captures HTTP request lines
//                  and `Cookie:` headers. Logs both raw and parsed
//                  format into /var/lib/void-os/loot/urls.csv.
//    1. COOKIES  — read back the captured URL log files and emit a
//                  cookies.csv with `(host, cookie)` rows.
//    2. MITM EN  — toggle ip_forward on Pi 5 (sysctl). On the soft AP
//                  (app_rogue) the device is the gateway; with this
//                  on, traffic from clients flows through the device
//                  to its upstream station interface for true MITM.
//
// Like app_rogue, the URL listener runs as a scheduled task while armed
// so the operator can navigate away and the sniffer stays online.

#pragma once
#include "os/events.h"

void app_sniff_init();
void app_sniff_tick();
void app_sniff_draw();
void app_sniff_event(Event e);
void app_sniff_suspend();
