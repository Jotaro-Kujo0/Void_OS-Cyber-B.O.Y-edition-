// test_hal_loot_stats.cpp — unit test for the pure stats computation in
// hal_loot. Feeds captured du/find + statvfs figures into
// hal_loot_stats_from() and asserts the derived LootStats, focusing on the
// fs_pct derivation and its clamp.
//
// Build + run from the project root:
//     g++ -std=c++17 -DVOIDOS_RPI5=1 -Isrc/rpi5 -Isrc -Isrc/hal \
//         tools/test_hal_loot_stats.cpp src/hal/hal_loot.cpp -o /tmp/loot_test
//     /tmp/loot_test
//
// Note: hal_loot.cpp pulls in <Arduino.h> from src/rpi5 on the Pi target
// and <SD.h> on ESP32, so compiling it host-side needs a stub. Building
// with -DVOIDOS_RPI5=1 and -Isrc/rpi5 selects the Pi branch; this test
// only exercises the pure function (no Arduino calls), so it compiles and
// links cleanly on a Linux host.
//
// NOTE: hal_loot.cpp also references hal_loot_write_csv etc. and the OS
// (millis) symbols, so to keep this standalone we #define a tiny millis()
// before including it.

#include "hal/hal_loot.h"
#include <cstdio>

// Provide the single Arduino symbol the surrounding file pulls in so the
// whole TU links host-side without the framework.
unsigned long millis();

unsigned long millis() { return 0; }

static int _failures = 0;
static int _checks   = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++_checks;                                                         \
        if (!(cond)) {                                                     \
            ++_failures;                                                   \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                  \
    } while (0)

static void test_basic() {
    // 100 of 1000 kB used => 10%.
    LootStats s = hal_loot_stats_from(7, 512000, 1000ull * 1000, 900ull * 1000);
    CHECK(s.files_count == 7);
    CHECK(s.bytes_written == 512000u);
    CHECK(s.fs_pct == 10);
}

static void test_full_volume() {
    // No free bytes => 100%.
    LootStats s = hal_loot_stats_from(0, 0, 100, 0);
    CHECK(s.fs_pct == 100);
}

static void test_free_over_total_clamps_to_zero() {
    // f_bavail*frsize can exceed f_blocks*frsize from measurement skew;
    // derived used must clamp to 0% rather than wrap negative.
    LootStats s = hal_loot_stats_from(1, 1, 100, 200);
    CHECK(s.fs_pct == 0);
}

static void test_zero_total() {
    // Zero-size volume must not divide-by-zero; reports 0%.
    LootStats s = hal_loot_stats_from(0, 0, 0, 0);
    CHECK(s.fs_pct == 0);
    CHECK(s.files_count == 0);
    CHECK(s.bytes_written == 0);
}

static void test_partial_pct() {
    // 1 of 3 blocks used => 33%.
    LootStats s = hal_loot_stats_from(3, 0, 3, 2);
    CHECK(s.fs_pct == 33);
}

int main() {
    test_basic();
    test_full_volume();
    test_free_over_total_clamps_to_zero();
    test_zero_total();
    test_partial_pct();

    std::printf("hal_loot_stats: %d checks, %d failures\n", _checks, _failures);
    return _failures ? 1 : 0;
}