// app_scan.h — small network recon sub-modes.
// ARP hosts / TCP ports / service hints / soft-AP stations.
// Share one host_log (32) so navigation keeps the working set.

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
