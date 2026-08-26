// hal_onewire_emul.h — iButton / Dallas 1-Wire emulation
//
// ───────────────────────────────────────────────────────────────────────────
//  BACKGROUND
// ───────────────────────────────────────────────────────────────────────────
//
//  The DS1990A is a 64-bit ROM ID-only iButton. A reader pulls the data
//  line LOW, then issues a 1-Wire reset / presence pulse. The DS1990A
//  answers with its 8-byte ROM: a 1-byte family code (0x01 for DS1990A),
//  a 6-byte unique serial number, and a 1-byte CRC8.
//
//  The legacy physical security world uses DS1990A keys for:
//      * Building / utility access (electric meter readers, gate openers)
//      * Time-and-attendance clocks
//      * PoS terminal "manager keys"
//
//  hal_onewire_emul turns the device into either a reader (the existing
//  PIN_ONEWIRE bit-banged implementation) OR an emulator (bit-banged in
//  the opposite direction). The emulator role requires driving the same
//  GPIO as the reader, with a pull-up enabled on the host side.
//
// ───────────────────────────────────────────────────────────────────────────
//  MODES
// ───────────────────────────────────────────────────────────────────────────
//
//   * DS1990_READ   — read any iButton that touches the reader probe.
//                     Stores the 8-byte ROM in a ring buffer.
//   * DS1990_EMUL   — emulate a previously-saved ROM. When the reader
//                     issues a presence pulse, the emulator responds with
//                     the stored ROM and 8 CRC8 bytes.
//   * DS1990_DUMP   — list all stored ROMs (last 16) and write them to
//                     /mnt/void-os/nfc/ibuttons_<UTC>.csv.
//
// ───────────────────────────────────────────────────────────────────────────
//  IMPLEMENTATION ROADMAP
// ───────────────────────────────────────────────────────────────────────────
//
//  1. GPIO open-drain bit-banging. On the Pi 5, both read and emulate
//     must use the libgpiod "set-line-value-with-debounce" flag so a
//     short on the probe does not latch up.
//
//  2. CRC8 implementation. The polynomial is 0x8C (x^8 + x^5 + x^4 + 1).
//
//  3. ROM storage. Up to IBUTTON_ROM_BYTES * 16 entries (128 bytes).
//
//  4. Latency. The 1-Wire reset pulse is 480 µs, the presence pulse is
//     240 µs; the read time slot is 60 µs..120 µs. The Pi 5 user-space
//     GPIO latency (≈ 30 µs) is fast enough but borderline. Consider
//     kernel-side GPIO with PREEMPT_RT for production.
//
// ───────────────────────────────────────────────────────────────────────────
//  SECURITY / ETHICAL POSTURE
// ───────────────────────────────────────────────────────────────────────────
//
//  * The skeleton never stores ROMs automatically. The app layer must
//    call hal_onewire_emul_save() explicitly.
//  * Emulation must require a UI confirmation in app_bus before the
//    stored ROM is emitted over the wire.
//  * When dark mode is active, both reader and emulator are disabled.
//
// ───────────────────────────────────────────────────────────────────────────

#pragma once
#include <stdint.h>
#include <stdbool.h>

// IBUTTON_ROM_BYTES is mirrored from config.h so this header does not need
// to pull in the whole chip config. The two definitions MUST stay in sync;
// any mismatch would be caught by the build below.
#define IBUTTON_ROM_BYTES  8
#define IBUTTON_LOG_MAX    16

typedef struct {
    uint8_t  rom[IBUTTON_ROM_BYTES];   // family + serial + CRC
    uint32_t timestamp_ms;
    char     label[16];               // optional user label
} IButtonRom;

typedef enum {
    DS1990_OFF   = 0,
    DS1990_READ  = 1,
    DS1990_EMUL  = 2,
    DS1990_DUMP  = 3,
} Ds1990Mode;

void     hal_onewire_emul_init();
void     hal_onewire_emul_tick();

// Mode control
void     hal_onewire_emul_set_mode(Ds1990Mode m);
Ds1990Mode hal_onewire_emul_get_mode();

// Capture a ROM read off the wire.
bool     hal_onewire_emul_pop(IButtonRom *out);

// Manual ROM storage / emulation target selection.
bool     hal_onewire_emul_save(const IButtonRom *r);
bool     hal_onewire_emul_recall(uint8_t slot, IButtonRom *out);
uint8_t  hal_onewire_emul_slot_count();

// UI: render the latest ROM as "AA:BB:CC:DD:EE:FF:GG:HH".
void     hal_onewire_emul_format(const IButtonRom *r, char *out, uint8_t out_len);