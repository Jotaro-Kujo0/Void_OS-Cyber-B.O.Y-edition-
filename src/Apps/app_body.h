// app_body.h — slot APP_BODY: passive Wi-Fi presence tracker (Pi 5)
//
// Captures 802.11 Probe Requests via tcpdump pipe, extracts MAC + SSID,
// and maintains a ring buffer of observed devices with OUI vendor lookup.
//
// Sub-modes:
//   0. SNIFF  — toggle probe-request capture on/off
//   1. LIST   — top entries sorted by probe count
//   2. VENDOR — OUI prefix lookup for selected entry
//   3. CLEAR  — wipe the ring buffer
//
// Cross-app: app_home status bar calls app_body_visible_count().

#pragma once
#include "os/events.h"
#include <stdint.h>

void    app_body_init();
void    app_body_tick();
void    app_body_draw();
void    app_body_event(Event e);
void    app_body_suspend();

// Ingest — called by the tcpdump pipe pump or from hal_wifi when a
// probe-request frame is decoded. Duplicate MACs increment count.
void    app_body_add_packet(const uint8_t *mac, const char *ssid);

// Cross-app accessor for app_home / status bar.
uint16_t app_body_visible_count();
