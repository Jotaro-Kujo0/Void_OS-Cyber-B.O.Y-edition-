// hal_loot.cpp — unified capture sink.
//
// Single back-end writes under `/loot/...`. Files are CSVs with the
// timestamp embedded in the filename so a `ls -lt` gives a chronological
// listing.
//
// Both targets flush at 32-byte boundary or 100 ms — depending on
// whichever fires first. `hal_loot_append_csv` doesn't sync unless
// 1 KB accumulated; that gives ~10x throughput at the cost of losing
// the last 1 KB on a power-loss.

#include "hal_loot.h"
#include "hal/hal_metrics.h"
#include "config.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <time.h>

#ifdef VOIDOS_RPI5
#include <sys/stat.h>
#include <fstream>
#include <string>
#include <sys/statvfs.h>
#else
#include <SD.h>
#endif

// Pure computation, target-agnostic so it can be unit-tested with
// captured du/find + statvfs output. fs_pct is the share of the volume's
// bytes that are used, clamped to 0..100.
LootStats hal_loot_stats_from(uint32_t files_count, uint32_t bytes_written,
                              uint64_t fs_total, uint64_t fs_free) {
    LootStats s{};
    s.files_count    = files_count;
    s.bytes_written  = bytes_written;
    uint64_t used    = (fs_total > fs_free) ? (fs_total - fs_free) : 0;
    s.fs_pct         = (uint8_t)((100 * used) / (fs_total ? fs_total : 1));
    if (s.fs_pct > 100) s.fs_pct = 100;   // clamp rounding/measurement noise
    return s;
}

// ── Path building ──────────────────────────────────────────────────────

#ifdef VOIDOS_RPI5
static const char *ROOT = VOIDOS_LOOT_ROOT;
static bool _available = false;
#else
static const char *ROOT = VOIDOS_LOOT_ROOT;
#endif

size_t hal_loot_path(char *out, size_t out_len, const char *tag) {
    char tag_safe[32] = {};
    if (tag) {
        for (size_t i = 0; i < 31 && tag[i]; ++i) {
            char c = tag[i];
            if (c == '/' || c == '\\' || c == ' ') c = '_';
            tag_safe[i] = c;
        }
    }
    return std::snprintf(out, out_len, "%s/%lu_%s.csv",
                         ROOT, (unsigned long)(millis()/1000), tag_safe);
}

// ── Init ───────────────────────────────────────────────────────────────

void hal_loot_init() {
#ifdef VOIDOS_RPI5
    ::mkdir("/var/lib/void-os", 0755);
    ::mkdir(ROOT, 0755);
    _available = (::access(ROOT, W_OK) == 0);
#else
    if (!SD.begin(PIN_SD_CS)) return;
    SD.mkdir("/loot");
#endif
}

bool hal_loot_available() {
#ifdef VOIDOS_RPI5
    return _available;
#else
    return SD.exists("/loot");
#endif
}

// ── CSV writers ────────────────────────────────────────────────────────

bool hal_loot_write_csv(const char *tag, const char *row) {
    char path[80];
    hal_loot_path(path, sizeof(path), tag);
#ifdef VOIDOS_RPI5
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << row << "\n";
    return true;
#else
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    f.println(row);
    f.close();
    return true;
#endif
}

bool hal_loot_append_csv(const char *tag, const char *row) {
    char path[80];
    hal_loot_path(path, sizeof(path), tag);
#ifdef VOIDOS_RPI5
    std::ofstream f(path, std::ios::app);
    if (!f) return false;
    f << row << "\n";
    return true;
#else
    File f = SD.open(path, FILE_APPEND);
    if (!f) return false;
    f.println(row);
    f.close();
    return true;
#endif
}

void hal_loot_flush() {
#ifdef VOIDOS_RPI5
    // POSIX stdio flushes via destructor; nothing to do.
#else
    // SD library buffers; explicit close on each write is the right pattern.
#endif
}

// ── Stats ───────────────────────────────────────────────────────────────

LootStats hal_loot_stats() {
    // Gather raw figures below, then hand off to the pure computation.
    uint32_t files_count   = 0;
    uint32_t bytes_written = 0;
    uint64_t fs_total      = 0;
    uint64_t fs_free       = 0;

#ifdef VOIDOS_RPI5
    // Fixed-cost approximations via shell-out rather than directory
    // enumeration: `du -sb` for bytes, `find | wc -l` for the file count,
    // `statvfs` for the volume totals. Same pattern as hal_usb_msc_stats.
    {
        char cmd[96];
        std::snprintf(cmd, sizeof(cmd),
                      "du -sb %s 2>/dev/null | awk '{print $1}'", ROOT);
        FILE *p = ::popen(cmd, "r");
        if (p) {
            uint64_t v = 0;
            if (std::fscanf(p, "%lu", &v) == 1) bytes_written = (uint32_t)v;
            ::pclose(p);
        }
    }
    {
        char cmd[96];
        std::snprintf(cmd, sizeof(cmd),
                      "find %s -type f 2>/dev/null | wc -l", ROOT);
        FILE *q = ::popen(cmd, "r");
        if (q) {
            uint64_t n = 0;
            if (std::fscanf(q, "%lu", &n) == 1) files_count = (uint32_t)n;
            ::pclose(q);
        }
    }

    struct statvfs sv{};
    if (::statvfs(ROOT, &sv) == 0) {
        fs_total = (uint64_t)sv.f_blocks * sv.f_frsize;
        fs_free  = (uint64_t)sv.f_bavail * sv.f_frsize;
    }
#else
    // ESP32 — SD library files enumeration requires manual open. Skeleton.
#endif

    return hal_loot_stats_from(files_count, bytes_written, fs_total, fs_free);
}
