// hal_metrics.h — periodic system-usage snapshot.
//
// One snapshot reads O(1) on ESP32 (FreeHeap, task count) and O(1 ish)
// on Pi 5 (`/proc/stat`, `sysinfo()`, `statvfs` on the loot dir).
// Called once per second from a scheduled task — the snapshot itself
// must NEVER block.
//
// Wire-up: `hal_metrics_init` registers a snapshot task; apps consume
// the result via `hal_metrics_snapshot()`. Modifying callers should
// query once and cache (it doesn't change in a single frame).

#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t  cpu_pct;          // 0–100 sched_load
    uint8_t  mem_pct;          // 0–100 used
    uint32_t mem_total_kb;
    uint32_t mem_free_kb;
    uint8_t  sdc_pct;          // 0 if no card
    uint32_t sdc_used_kb;
    uint32_t sdc_total_kb;
    uint32_t net_rx_total_kb;  // cumulative bytes from /proc/net/dev (Pi 5)
    uint32_t net_tx_total_kb;  // cumulative bytes from /proc/net/dev (Pi 5)
    uint8_t  task_count;       // scheduled-task count
    uint32_t uptime_s;
    uint16_t load_avg_1m_x100; // (loadavg * 100) — Pi 5 only; 0 on ESP32
} SystemUsage;

void         hal_metrics_init();
SystemUsage  hal_metrics_snapshot();

// ── Watchdog ────────────────────────────────────────────────────────────
//
// Apps touch `hal_metrics_heartbeat("tag")` at safe points. If a tag is
// not touched for `WATCHDOG_TIMEOUT_MS`, the snapshot populates
// `watchdog_stalled` accordingly and the operator sees a banner.
//
// Max 4 tags. Use sparingly — only on long-running loops.

#define WATCHDOG_TAGS_MAX    4
#define WATCHDOG_TIMEOUT_MS  6000

typedef struct {
    bool     ok;
    uint8_t  stalled_tag;       // first non-fresh tag
    char     tag_name[WATCHDOG_TAGS_MAX][16];
    uint32_t last_seen_ms[WATCHDOG_TAGS_MAX];
} WatchdogState;

void hal_metrics_heartbeat(const char *tag);
void hal_metrics_clear_heartbeat(const char *tag);
WatchdogState hal_metrics_watchdog_snapshot();
