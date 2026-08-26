// app_scan.h — slot APP_SCAN: small network reconnaissance sub-modes.
//
// Sub-modes:
//    0. ARP SCAN  — populate a MAC + IP list for the local subnet. Pi 5
//                   reads `/proc/net/arp` and falls back to a targeted
//                   ICMP ping sweep if the table is empty. ESP32 reads
//                   the connected AP's DHCP lease list when in station
//                   mode.
//    1. TCP PORTS — TCP-connect scan against a list of common service
//                   ports on either the gateway IP from ARP SCAN or the
//                   operator-supplied target in `scan_target` hal_storage
//                   key.
//    2. SERVICES  — operator-facing port → service hint (read-only dict).
//                   Tells them what `22` likely means.
//    3. WIFI HOSTS — list of recent stations on the same AP (when running
//                   as a soft AP via app_rogue).
//
// All sub-modes share `host_log` (last 32 entries) so the operator can
// navigate between them without losing the working set.

#pragma once
#include "../os/events.h"

void app_scan_init();
void app_scan_tick();
void app_scan_draw();
void app_scan_event(Event e);
void app_scan_suspend();

uint16_t app_scan_host_count();
const char *app_scan_host_at(uint16_t i);

uint16_t app_scan_port_count();
const char *app_scan_port_at(uint16_t i);
