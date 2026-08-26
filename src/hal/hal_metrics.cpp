// hal_metrics.cpp — periodic system usage snapshot.
//
// Pi 5 path:
//   * CPU%: uses sched_load() for the in-OS scheduler; blends it with
//     /proc/stat-derived 1-minute load on the underlying kernel.
//   * Mem:  sysinfo() returns total/free in pages.
//   * SDC:  statvfs on /var/lib/void-os. Falls back to /tmp.
//   * Net:  /proc/net/dev totals rx/tx bytes across the named IFs.
//
// ESP32:
//   * CPU%: sched_load() in this OS only.
//   * Mem:  ESP.getFreeHeap() / getHeapSize().
//   * SDC:  SD.usedBytes() / totalBytes() (SdFat-style). Stubbed when
//           the legacy SD library doesn't expose the counters.
//   * Net:  not surfaced (WiFi.isConnected() boolean only).

#include "hal_metrics.h"
#include "os/scheduler.h"
#include "config.h"

#include <cstdio>
#include <cstring>
#include <Arduino.h>

#ifdef VOIDOS_RPI5
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>
#endif

#ifndef VOIDOS_RPI5
#include <Arduino.h>
#ifdef ESP32
#include <esp_system.h>
#endif
#endif

// ── Watchdog ring ──────────────────────────────────────────────────────

static WatchdogState _wd = { .ok = true };

void hal_metrics_heartbeat(const char *tag) {
    if (!tag) return;
    uint32_t now = millis();
    // Find or append.
    int slot = -1;
    for (uint8_t i = 0; i < WATCHDOG_TAGS_MAX; ++i) {
        if (_wd.tag_name[i][0] == 0) { slot = i; break; }
        if (std::strcmp(_wd.tag_name[i], tag) == 0) { slot = i; break; }
    }
    if (slot < 0) return;
    std::strncpy(_wd.tag_name[slot], tag, sizeof(_wd.tag_name[0]) - 1);
    _wd.tag_name[slot][sizeof(_wd.tag_name[0]) - 1] = 0;
    _wd.last_seen_ms[slot] = now;
    _wd.ok = true;
}

void hal_metrics_clear_heartbeat(const char *tag) {
    if (!tag) return;
    for (uint8_t i = 0; i < WATCHDOG_TAGS_MAX; ++i) {
        if (std::strcmp(_wd.tag_name[i], tag) == 0) {
            _wd.tag_name[i][0] = 0;
            _wd.last_seen_ms[i] = 0;
            return;
        }
    }
}

WatchdogState hal_metrics_watchdog_snapshot() {
    WatchdogState out = _wd;
    out.stalled_tag = 0xFF;
    uint32_t now = millis();
    for (uint8_t i = 0; i < WATCHDOG_TAGS_MAX; ++i) {
        if (!_wd.tag_name[i][0]) continue;
        if (now - _wd.last_seen_ms[i] > WATCHDOG_TIMEOUT_MS) {
            out.ok = false;
            if (out.stalled_tag == 0xFF) out.stalled_tag = i;
        }
    }
    return out;
}

// ── Heartbeat scorer (called once per second) ──────────────────────────

static int8_t _scorer_id = -1;

static void scorer_tick() {
    WatchdogState s = hal_metrics_watchdog_snapshot();
    if (!s.ok) {
        // Mark a threshold-crossed watchpoint but never panic. The
        // operator UI surfaces this in app_stat as a red banner.
    }
}

// ── snapshot ──────────────────────────────────────────────────────────

#ifdef VOIDOS_RPI5
static uint64_t _last_total_jiffies = 0;
static uint64_t _last_idle_jiffies = 0;

static void read_cpu_jiffies(uint64_t &total, uint64_t &idle) {
    total = idle = 0;
    std::ifstream f("/proc/stat");
    if (!f) return;
    char line[256];
    if (!f.getline(line, sizeof(line))) return;
    // First line: cpu  user nice system idle iowait irq softirq steal guest guest_nice
    unsigned long long v[10] = {0};
    int got = std::sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                          &v[0], &v[1], &v[2], &v[3], &v[4],
                          &v[5], &v[6], &v[7], &v[8], &v[9]);
    if (got < 4) return;
    for (int i = 0; i < got; ++i) total += v[i];
    idle = v[3] + v[4]; // idle + iowait
}
#endif

SystemUsage hal_metrics_snapshot() {
    SystemUsage s{};
    s.cpu_pct = 100;  // placeholder
    s.task_count = 0; // filled by scheduler hook below
    s.uptime_s = millis() / 1000;

#ifdef VOIDOS_RPI5
    // Memory
    struct sysinfo si {};
    if (::sysinfo(&si) == 0) {
        s.mem_total_kb = (uint32_t)(si.totalram / 1024);
        s.mem_free_kb  = (uint32_t)(si.freeram / 1024);
        s.mem_pct = (uint8_t)((100 * (si.totalram - si.freeram)) / (si.totalram ? si.totalram : 1));
    }
    // Disk
    struct statvfs sv {};
    if (::statvfs("/var/lib/void-os", &sv) == 0 || ::statvfs("/tmp", &sv) == 0) {
        uint64_t size = (uint64_t)sv.f_blocks * sv.f_frsize;
        uint64_t freeb = (uint64_t)sv.f_bavail * sv.f_frsize;
        s.sdc_total_kb = (uint32_t)(size / 1024);
        s.sdc_used_kb  = (uint32_t)((size - freeb) / 1024);
        if (s.sdc_total_kb) s.sdc_pct = (uint8_t)((100 * s.sdc_used_kb) / s.sdc_total_kb);
    }
    // Net cumulative (sum across common interfaces)
    std::ifstream f("/proc/net/dev");
    if (f) {
        char line[256];
        // skip first 2 header lines
        f.getline(line, sizeof(line));
        f.getline(line, sizeof(line));
        while (f.getline(line, sizeof(line))) {
            char ifname[16] = {};
            unsigned long rx = 0, tx = 0;
            if (std::sscanf(line, " %15[^:]: %lu %*u %*u %*u %*u %*u %*u %*u %lu",
                             ifname, &rx, &tx) == 3) {
                if (std::strncmp(ifname, "lo", 2) == 0) continue;
                s.net_rx_total_kb += (uint32_t)(rx / 1024);
                s.net_tx_total_kb += (uint32_t)(tx / 1024);
            }
        }
    }
    // OS-level CPU% via kernel load average
    std::ifstream la("/proc/loadavg");
    if (la) { float v; la >> v; s.load_avg_1m_x100 = (uint16_t)(v * 100.0f); }
#else
    s.mem_total_kb = ESP.getHeapSize() / 1024;
    s.mem_free_kb  = ESP.getFreeHeap() / 1024;
    s.mem_pct      = (uint8_t)(100 - (100 * (uint32_t)ESP.getFreeHeap()) /
                                (ESP.getHeapSize() ? ESP.getHeapSize() : 1));
#endif

    // Scheduler CPU% + task count come from the scheduler module.
    s.cpu_pct = sched_load();
    return s;
}

// ── init ───────────────────────────────────────────────────────────────

void hal_metrics_init() {
    if (_scorer_id < 0)
        _scorer_id = sched_add("metrics_scorer", scorer_tick, 1000, 9);
}
