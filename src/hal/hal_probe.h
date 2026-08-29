// hal_probe.h — shared passive Wi-Fi probe-request capture (Pi 5)
//
// Owns the single `tcpdump -e` pipe + device ring that BOTH app_body and
// app_maraud were previously duplicating. Apps register a sink callback
// to be notified of each decoded (mac, ssid); duplicate MACs bump a per-
// entry count in the shared ring. Starting capture twice is harmless (the
// second caller just sees the same running session).
//
// RPI5 path forks tcpdump on wlan0mon. On targets without tcpdump or a
// monitor VIF every call degrades to false — no crash.

#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef void (*ProbeSink)(const uint8_t *mac, const char *ssid);

typedef struct {
    uint8_t  mac[6];
    char     ssid[33];
    uint32_t count;
    uint32_t last_ms;     // monotonic ms of last sighting
} ProbeDevice;

#define PROBE_RING_MAX 64

// Lifecycle ------------------------------------------------------------
void   hal_probe_init();
bool   hal_probe_capture_start();
void   hal_probe_capture_stop();
bool   hal_probe_running();
void   hal_probe_tick();       // call every frame; drains the tcpdump pipe

// Notification ----------------------------------------------------------
// Register a callback invoked (from hal_probe_tick) for every decoded
// probe. Pass nullptr to clear. Multiple sinks are not supported — the
// two consumers call this once in their init.
void   hal_probe_register_sink(ProbeSink s);

// Ring -----------------------------------------------------------------
uint16_t      hal_probe_visible_count();
const ProbeDevice *hal_probe_at(uint16_t i);
void          hal_probe_clear();