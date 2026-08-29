// test_hal_usb_msc_stats.cpp — unit test for the pure stats computation
// in hal_usb_msc. Feeds captured du/find values into
// hal_usb_msc_stats_from() and asserts the derived UsbMscStats, focusing
// on the free-space clamping and active/file propagation.
//
// Build + run from the project root:
//     g++ -std=c++17 -Isrc -Isrc/hal \
//         tools/test_hal_usb_msc_stats.cpp src/hal/hal_usb_msc.cpp -o /tmp/msc_test
//     /tmp/msc_test
//
// The test compiles the real HAL translation unit so the computation
// under test is the production one. It only exercises the pure function,
// so no root/configfs is required.

#include "hal/hal_usb_msc.h"
#include <cstdio>

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

static void test_basic_fields() {
    UsbMscStats s = hal_usb_msc_stats_from(8192, 1000, 7, true);
    CHECK(s.active == true);
    CHECK(s.capacity_kb == 8192);
    CHECK(s.files_count == 7);
    CHECK(s.free_kb == 8192u - 1000u);   // 7192
}

static void test_exact_fit() {
    // used == capacity => free clamps to 0, not negative.
    UsbMscStats s = hal_usb_msc_stats_from(4096, 4096, 3, false);
    CHECK(s.active == false);
    CHECK(s.files_count == 3);
    CHECK(s.free_kb == 0);
}

static void test_over_capacity_clamps_to_zero() {
    // used > capacity (e.g. host-side loot larger than the image after
    // FAT overhead, or a stale du) must never go negative.
    UsbMscStats s = hal_usb_msc_stats_from(2048, 5000, 12, true);
    CHECK(s.active == true);
    CHECK(s.capacity_kb == 2048);
    CHECK(s.files_count == 12);
    CHECK(s.free_kb == 0);
}

static void test_zero_all() {
    // The ESP32 stub path resolves here: nothing gathered => all zeros.
    UsbMscStats s = hal_usb_msc_stats_from(0, 0, 0, false);
    CHECK(s.active == false);
    CHECK(s.capacity_kb == 0);
    CHECK(s.files_count == 0);
    CHECK(s.free_kb == 0);
}

static void test_one_kb_granularity() {
    // Image just one kB larger than used => exactly 1 free.
    UsbMscStats s = hal_usb_msc_stats_from(1000, 999, 0, true);
    CHECK(s.free_kb == 1);
}

int main() {
    test_basic_fields();
    test_exact_fit();
    test_over_capacity_clamps_to_zero();
    test_zero_all();
    test_one_kb_granularity();

    std::printf("hal_usb_msc_stats: %d checks, %d failures\n", _checks, _failures);
    return _failures ? 1 : 0;
}