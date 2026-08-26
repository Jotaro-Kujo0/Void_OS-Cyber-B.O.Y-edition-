// app_log.h — Field logger & PCAP/CSV/GPX writer (slot APP_LOG)
//
// ───────────────────────────────────────────────────────────────────────────
//  OVERVIEW
// ───────────────────────────────────────────────────────────────────────────
//
//  app_log is the central router for "what gets written to the SD card".
//  It owns:
//
//      * PCAP files for Wi-Fi / BLE / CC1101 captures
//      * CSV files for wardriving and bus intercept
//      * GPX track files for the heatmap
//      * An INDEX.tsv that records every captured session with its
//        start time, duration, file name, and SHA-256 prefix
//
//  The app does NOT generate the captures itself — that is the job of
//  app_wifi, app_radio, app_bus. It just receives the records and
//  streams them to /mnt/void-os via hal_sdcard.
//
//  Each sub-mode is selectable from a single-screen menu:
//
//      1. PCAP  — open /mnt/void-os/captures/wifi_<UTC>.pcap, write
//                  the libpcap global header, and start streaming
//                  frames from hal_wifi_capture_consume().
//      2. CSV   — append a row to /mnt/void-os/wardriving_<UTC>.csv
//                  every time app_wifi pumps a new AP record.
//      3. GPX   — append a <trkpt> every 1 s to
//                  /mnt/void-os/gpx/track_<UTC>.gpx using the GPS
//                  fix from hal_gps_lat() / hal_gps_lon().
//      4. HEAT  — every captured AP contributes a row to
//                  /mnt/void-os/heatmap/heatmap_<UTC>.csv with
//                  lat / lon / rssi / band / ssid.
//      5. WORD  — open a wordlist file from /mnt/void-os/wordlists/
//                  via hal_sdcard_wordlist_open() and stream one
//                  line at a time. This is the data source for
//                  hashcat-compatible WPA cracking attempts in
//                  app_wifi (handshake capture) once they are wired
//                  together.
//
//  The menu also exposes a "Card: NN% used" indicator from
//  hal_sdcard_percent_used() so the operator knows when to swap cards.
//
// ───────────────────────────────────────────────────────────────────────────
//  PCAP FILE FORMAT (libpcap classic, link-layer LINKTYPE_IEEE802_11_RADIO)
// ───────────────────────────────────────────────────────────────────────────
//
//      Magic            : 0xA1B2C3D4  (microsecond timestamps)
//      Major / minor    : 2 / 4
//      Snaplen          : 65535
//      Link type        : 127 (LINKTYPE_IEEE802_11_RADIO)
//
//  Each packet record:
//
//      ts_sec           : 4 bytes
//      ts_usec          : 4 bytes
//      incl_len         : 4 bytes  (clamped to PCAP_SNAP_LEN)
//      orig_len         : 4 bytes
//      data             : incl_len bytes
//
//  The radiotap header is prepended to each 802.11 frame by the
//  monitor-mode VIF so the consumer (Wireshark, tshark) decodes it
//  directly.
//
// ───────────────────────────────────────────────────────────────────────────

#pragma once
#include "../os/events.h"

void app_log_init();
void app_log_tick();
void app_log_draw();
void app_log_event(Event e);
void app_log_suspend();