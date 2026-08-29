// app_maraud.h — Marauder-style Wi-Fi attack/monitor sheet.
// PWND/BEACON/DEAUTH/SCOUT; gated on monitor mode, refused in stealth.

#pragma once
#include "os/events.h"

void app_maraud_init();
void app_maraud_tick();
void app_maraud_draw();
void app_maraud_event(Event e);
void app_maraud_suspend();

// cross-app ingest for pwn/probe rows (mirror of app_body_add_packet)
void app_maraud_add_packet(const uint8_t *mac, const char *ssid);
uint16_t app_maraud_visible_count();