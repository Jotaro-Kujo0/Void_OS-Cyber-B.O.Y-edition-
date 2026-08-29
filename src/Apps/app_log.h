// app_log.h — central SD-card writer (router for captures).
// Lives in PCAP (wifi/ble/cc1101), CSV (wardriving), GPX (track),
// HEAT (rssi heatmap), WORD (wordlist stream) sub-modes.
// Receives records from app_wifi/radio/bus; owns all file output.
// PCAP global header: magic 0xA1B2C3D4, ver 2/4, snaplen 65535,
// linktype 127 (LINKTYPE_IEEE802_11_RADIO); radiotap prepended.

#pragma once
#include "../os/events.h"

void app_log_init();
void app_log_tick();
void app_log_draw();
void app_log_event(Event e);
void app_log_suspend();