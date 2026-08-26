// hal_sdcard.h — persistent field storage for PCAP, wordlists, GPX/CSV logs
//
// ───────────────────────────────────────────────────────────────────────────
//  ARCHITECTURE
// ───────────────────────────────────────────────────────────────────────────
//
//  hal_sdcard is the single point of truth for on-card persistence. It owns
//  the SPI mode used for the breakout (the BOM calls out an SPI-compatible
//  microSD breakout on PIN_SD_CS), and exposes a small POSIX-style surface so
//  the apps above it never need to touch spidev or FAT32 details.
//
//  On Raspberry Pi 5 the cleanest path is:
//      * Mount a FAT32 partition at /mnt/void-os at boot
//      * Treat /mnt/void-os as the "sd" namespace
//      * Fall back to $XDG_STATE_HOME/void-os/sd if the card is absent
//
//  On ESP32 the SD library is used; the fallback is NVS (already covered
//  by hal_storage).
//
// ───────────────────────────────────────────────────────────────────────────
//  WHAT THIS MODULE OWNS
// ───────────────────────────────────────────────────────────────────────────
//
//   .pcap files          : Wi-Fi, BLE, and CC1101 captures. PCAP file format
//                          is implemented in hal_pcap.h. The application code
//                          calls hal_sdcard_open("/captures/wifi_001.pcap")
//                          and then hal_pcap_write_packet() to append.
//
//   .csv / .gpx files    : wardriving GPS track, signal heatmap. See app_log.
//
//   wordlists/           : credential dictionaries, default RFID keys, IR
//                          code databases. Files are expected to be present
//                          on the card before launch; hal_sdcard just opens
//                          them read-only and lets the caller stream.
//
//   nfc/, ir/, rf/       : app-private folders that hold NDEF dumps, IR
//                          captures, and raw RF captures in human-readable
//                          text form. Names are chosen by the apps.
//
// ───────────────────────────────────────────────────────────────────────────
//  WHAT MUST BE ADDED BEFORE THIS MODULE IS USABLE
// ───────────────────────────────────────────────────────────────────────────
//
//  1. A mount step in hal_sdcard_init():
//        if (mount("/dev/mmcblk0p1", "/mnt/void-os", "vfat",
//                  MS_NOATIME, "utf8,noexec,nodev") != 0)
//            return false;
//     The Pi 5 path /dev/mmcblk0p1 depends on whether the bootloader sees
//     the card as MMC or as USB; iterate over /dev/mmcblk0p* and
//     /dev/sda1 at boot and pick the first one that mounts cleanly.
//
//  2. Directory bootstrap (idempotent):
//        mkdir_p("/mnt/void-os/captures");
//        mkdir_p("/mnt/void-os/wordlists");
//        mkdir_p("/mnt/void-os/gpx");
//        mkdir_p("/mnt/void-os/heatmap");
//        mkdir_p("/mnt/void-os/nfc");
//        mkdir_p("/mnt/void-os/ir");
//        mkdir_p("/mnt/void-os/rf");
//
//  3. Path sandboxing. Every public function that accepts a relative path
//     must prepend "/mnt/void-os/" and reject ".." segments. The current
//     skeleton rejects empty paths only.
//
//  4. Atomic write helper used by hal_storage_set_blob's hot path on Pi 5
//     so that a power-loss mid-write does not corrupt a previously saved
//     capture index.
//
//  5. A `hal_sdcard_format()` routine that is gated behind a UI confirmation
//     and never invoked automatically. See void-os/feature-flags.
//
//  6. The current skeleton returns "unavailable" for every operation. The
//     app layer treats "unavailable" as a soft failure and falls back to
//     the in-memory ring buffer.
//
// ───────────────────────────────────────────────────────────────────────────
//  RESOURCE NOTES (Pi 5, 4 GB)
// ───────────────────────────────────────────────────────────────────────────
//
//   * Mount buffer:    ~256 KB (kernel page cache, not in process RSS)
//   * Per-open FILE*:  ~4 KB kernel-side
//   * Concurrent opens: keep under 8; the SD card's FAT directory cache
//                       collapses quickly above 16.
//   * Streaming write:  4-byte aligned buffers are ~10% faster than
//                       arbitrary-length writes on the SPI bus.
//
// ───────────────────────────────────────────────────────────────────────────

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Lifecycle -----------------------------------------------------------------
void   hal_sdcard_init();
bool   hal_sdcard_available();          // true if /mnt/void-os is mounted
void   hal_sdcard_tick();               // background sync / cache flush

// File I/O ------------------------------------------------------------------
// All paths are interpreted as relative to the SD mount point and must not
// contain ".." segments. Returns NULL on failure.
void  *hal_sdcard_open(const char *path, bool write);  // returns FILE*-like
bool   hal_sdcard_close(void *handle);
size_t hal_sdcard_write(void *handle, const void *buf, size_t len);
size_t hal_sdcard_read (void *handle, void *buf, size_t len);
bool   hal_sdcard_seek (void *handle, uint32_t offset);
bool   hal_sdcard_truncate(void *handle, uint32_t length);
bool   hal_sdcard_exists(const char *path);
bool   hal_sdcard_remove(const char *path);
bool   hal_sdcard_mkdir (const char *path);

// Directory walk ------------------------------------------------------------
// Returns the next entry name into `out` (null-terminated, max `out_len`).
// Call hal_sdcard_dir_close() when done. Returns false at end-of-directory
// or on error.
void  *hal_sdcard_dir_open(const char *path);
bool   hal_sdcard_dir_next(void *handle, char *out, size_t out_len);
void   hal_sdcard_dir_close(void *handle);

// Wordlist streaming --------------------------------------------------------
// Wordlists may not fit in RAM (the bundled common-credentials file is
// 14 MB and the WPA PSK dictionary is ~600 MB). Stream one line at a time
// into a caller-provided buffer. Returns false at EOF or error.
void  *hal_sdcard_wordlist_open(const char *path);
bool   hal_sdcard_wordlist_next(void *handle, char *line, size_t line_len);
void   hal_sdcard_wordlist_close(void *handle);

// Filesystem accounting -----------------------------------------------------
uint32_t hal_sdcard_total_bytes();
uint32_t hal_sdcard_free_bytes();
uint8_t  hal_sdcard_percent_used();