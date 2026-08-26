// hal_loot.h — unified capture sink.
//
// Apps write here; the operator can grep one directory for any
// captured row. Path canonical:
//   * Pi 5:   /var/lib/void-os/loot/  (default; falls back to /tmp)
//   * ESP32:  /sd/loot/               (default; falls back to NVS
//                                  blobby 32 KB ring if no SD)
//
// CSV rows are flushed line-by-line. Sub-second timing is feasible at
// 115 KB/s sustained write on the SD card; capture subsystem throttles
// itself if the FS rejects writes (returns errno ENOSPC).

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void     hal_loot_init();
bool     hal_loot_available();
void     hal_loot_flush();

// Write `/loot/<UTC>_<tag>.csv` and return its path in `out`. UTF-8.
bool     hal_loot_write_csv(const char *tag, const char *row);

// Same as above but append. Useful for streaming events.
bool     hal_loot_append_csv(const char *tag, const char *row);

// Generate a fresh loot path under the active sink (doesn't open a
// file). For callers wanting to manage the file themselves.
size_t   hal_loot_path(char *out, size_t out_len, const char *tag);

// Subsystem stats — surfaced in app_stat.
typedef struct {
    uint32_t files_count;
    uint32_t bytes_written;
    uint32_t fs_pct;
} LootStats;
LootStats hal_loot_stats();
