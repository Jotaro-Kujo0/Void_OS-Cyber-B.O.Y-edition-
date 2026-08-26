// hal_mqtt.cpp — MQTT publisher SKELETON.
//
// =====================================================================
//  IMPLEMENTATION GUIDE
// =====================================================================
//
//  ponytail: lightweight publish-only client. Two transport choices:
//
//  ── mbedtls built-in (ESP32 bundled) ─────────────────────────────
//      * Open TCP socket to broker:port.
//      * Manually compose CONNECT packet:
//          byte 0:       0x10            // CONNECT
//          bytes 1-2:    remaining len
//          bytes 3-6:    "MQTT" (length-prefixed)
//          byte 7:       0x04            // version 3.1.1
//          bytes 8-12:   flags (clean session=2, username=80)
//          payload: client ID (16-byte UUID), username, password
//      * Read CONNACK (expects 0x20 0x02 0x00 0x00).
//      * Compose PUBLISH packet and call send repeatedly.
//
//  ── Paho Embedded C (multi-platform, also bundled) ───────────────
//
//      * `Eclipse-Paho/MQTT-C` rocks for the Pi 5 path (no new
//        package dep on Pi OS: `apt install libpaho-mqtt-dev`).
//      * On ESP32: vendor the source into `third_party/`.
//
//  ── TOPIC NAMING ──────────────────────────────────────────────────
//
//      void-os/<serial>/status           JSON snapshot every 5 s
//      void-os/<serial>/capture          one event per capture
//      void-os/<serial>/alert            one-shot on ROGUE / etc.
//
//  =====================================================================

#include "hal_mqtt.h"
#include <cstring>

static bool _connected = false;

void hal_mqtt_init()        { _connected = false; }

bool hal_mqtt_connect(const char *broker, uint16_t port) {
    if (!broker) return false;
    (void)port;
    _connected = true;  // stub: no real socket here yet
    return true;
}

bool hal_mqtt_publish(const char *topic, const char *payload) {
    if (!_connected || !topic || !payload) return false;
    // Real path: build PUBLISH packet, write to socket.
    (void)topic;
    (void)payload;
    return true;
}

void hal_mqtt_disconnect() { _connected = false; }
