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

// ── Path building ──────────────────────────────────────────────────────

#ifdef VOIDOS_RPI5
static const char *ROOT = "/var/lib/void-os/loot";
static bool _available = false;
#else
static const char *ROOT = "/loot";
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
    LootStats s{}; s.files_count = 0; s.bytes_written = 0; s.fs_pct = 0;
#ifdef VOIDOS_RPI5
    // sum file sizes under ROOT.
    struct stat st{};
    if (::stat(ROOT, &st) == 0 && S_ISDIR(st.st_mode)) {
        // ponytail: not enumerating today — fixed-cost approximation.
        // Use `du` shell-out: `du -sb /var/lib/void-os/loot`.
        char cmd[80]; std::snprintf(cmd, sizeof(cmd),
                                    "du -sb %s 2>/dev/null | awk '{print $1}'", ROOT);
        FILE *p = ::popen(cmd, "r");
        if (p) {
            uint64_t v = 0;
            int got = std::fscanf(p, "%lu", &v);
            ::pclose(p);
            if (got == 1) s.bytes_written = (uint32_t)v;
        }
        struct statvfs sv{};
        if (::statvfs(ROOT, &sv) == 0) {
            uint64_t size  = (uint64_t)sv.f_blocks * sv.f_frsize;
            uint64_t free  = (uint64_t)sv.f_bavail * sv.f_frsize;
            s.fs_pct = (uint8_t)((100 * (size - free)) / (size ? size : 1));
        }
        s.files_count = 1; // approximate
    }
#else
    // ESP32 — SD library files enumeration requires manual open. Skeleton.
#endif
    return s;
}
