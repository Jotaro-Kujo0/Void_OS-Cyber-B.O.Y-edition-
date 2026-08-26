// hal_wiegand.h — Wiegand reader intercept (D0 / D1 bit streams)
//
// ───────────────────────────────────────────────────────────────────────────
//  BACKGROUND
// ───────────────────────────────────────────────────────────────────────────
//
//  The vast majority of legacy physical access-control readers emit card
//  data over a Wiegand interface. The two data lines are open-collector:
//  the reader pulls D0 LOW for a "0" bit and D1 LOW for a "1" bit. Between
//  bits the lines float back to HIGH (the panel supplies a pull-up).
//
//  Standard bit widths:
//
//      Wiegand 26 bit   : E XXXXX XXXXXXXX XXXXXXXX O
//                         |  facility   card number  |
//      Wiegand 34 bit   : E XXXXXXXX XXXXXXXXXXXXXXX XXXXXXXX O
//      Wiegand 37 bit   : E XXXXXXXXX XXXXXXXXXXXXXXX XXXXXXXX O
//      Wiegand 42 bit   : used by HID iCLASS 15693 emulation (rare)
//
//  E = even parity over the next 12 bits, O = odd parity over the previous
//  12 bits. The decoder must verify both parities before accepting the
//  read.
//
// ───────────────────────────────────────────────────────────────────────────
//  IMPLEMENTATION ROADMAP
// ───────────────────────────────────────────────────────────────────────────
//
//  1. GPIO interrupts. The Pi 5 has no native edge-detection ISR; the
//     cheapest path is to poll D0 and D1 every 200 µs from a dedicated
//     thread. Each transition is timestamped with monotonic clock().
//
//  2. Pulse-width measurement. A valid Wiegand pulse is 20 µs..100 µs.
//     Reject anything outside that window as noise.
//
//  3. Inter-bit gap. The reader pulls the line back HIGH between bits;
//     the gap between consecutive bits is 1 ms..100 ms. If the gap
//     exceeds 100 ms, treat the previous bits as a complete frame.
//
//  4. Parity. Even and odd parity cover 12 bits each. Reject frames
//     with bad parity before reporting them to the caller.
//
//  5. Storage. The skeleton logs to RAM only. Real implementation
//     streams each accepted frame as one CSV row to
//     /mnt/void-os/nfc/wiegand_<UTC>.csv via hal_sdcard_open().
//
// ───────────────────────────────────────────────────────────────────────────
//  SECURITY / ETHICAL POSTURE
// ───────────────────────────────────────────────────────────────────────────
//
//  * The skeleton only counts events. No card numbers are stored until
//    the user explicitly enables capture.
//  * The UI must display a prominent "WIEGAND CAPTURE ENABLED" banner
//    while the feature is active. The status LED on the Pi must pulse
//    haptic-only (no RGB) so a casual bystander can tell.
//
// ───────────────────────────────────────────────────────────────────────────
//  RESOURCE NOTES (Pi 5, 4 GB)
// ───────────────────────────────────────────────────────────────────────────
//
//   * Polling thread CPU: ~0.5 % at 5 kHz poll rate
//   * RAM: bounded by WIEGAND_MAX_BITS (default 64 bits ≈ 8 bytes)
//   * No additional SPI / I2C traffic
//
// ───────────────────────────────────────────────────────────────────────────

#pragma once
#include <stdint.h>
#include <stdbool.h>

// Mirror of WIEGAND_MAX_BITS from config.h (kept literal so this header
// can stand alone; keep in sync with config.h if either value changes).
#define WIEGAND_MAX_BITS  64

typedef struct {
    uint64_t bits;             // raw bit pattern, LSB first
    uint8_t  length;           // number of bits captured (typically 26/34)
    bool     parity_ok;        // true if E and O bits verified
    uint32_t timestamp_us;     // monotonic µs at the start of the frame
} WiegandFrame;

void     hal_wiegand_init();
void     hal_wiegand_tick();                  // polls D0/D1 every frame

// Non-blocking: returns the most recent accepted frame, or false if none.
bool     hal_wiegand_pop(WiegandFrame *out);

// Convenience: format a frame as 8/16 hex digits depending on bit length.
void     hal_wiegand_format(const WiegandFrame *f, char *out, uint8_t out_len);

// Counters
uint32_t hal_wiegand_total_frames();
uint32_t hal_wiegand_parity_errors();

// Enable / disable capture. When disabled, every pop() returns false.
void     hal_wiegand_set_enabled(bool enabled);
bool     hal_wiegand_is_enabled();