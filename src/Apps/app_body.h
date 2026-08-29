// app_body.h — passive Wi-Fi presence tracker via tcpdump pipe.
// SNIFF / LIST / VENDOR / CLEAR probe requests + OUI lookup.

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
