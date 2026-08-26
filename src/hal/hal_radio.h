// hal_radio.h
#pragma once
#include <stdint.h>
#include <stdbool.h>

void hal_radio_init();
uint8_t hal_radio_rssi();              // Signal strength 0-255
uint8_t hal_radio_scan();              // Detected signal count
void hal_radio_send(float freq_mhz, const uint8_t *data, uint8_t len);
int8_t hal_radio_receive(uint8_t *buf, uint8_t *len);  // Returns -1 if no data

// ── Sub-GHz audit extensions ─────────────────────────────────────────────
//
//  All of the following require a Linux CC1101 driver that exposes GDO0
//  as a pollable GPIO. The base Cc1101Linux class in src/rpi5/ only ships
//  register / FIFO primitives; the audit routines below call into it
//  once a regional profile is selected (see the notes in each function).
//
//  On ESP32 the SmartRC-CC1101-Driver-Lib already exposes all the
//  primitives needed (setMHZ, setModulation, getRssi, ReceiveData, etc.)
//  so the ESP32 path is just glue.
//
//  Frequency bands covered (select via hal_radio_set_band()):
//
//      BAND_315   — 315 MHz ISM (US/AS band)
//      BAND_433   — 433.92 MHz ISM (EU/AS short-range)
//      BAND_868   — 868.3 MHz SRD (EU short-range)
//      BAND_915   — 915 MHz ISM (US/AS)
//
//  Modulation profiles selectable via hal_radio_set_modulation():
//
//      MOD_ASK_OOK    — On-Off Keying (most legacy gate openers)
//      MOD_ASK_RAW
//      MOD_FSK_1_2    — Frequency Shift Keying 1.2 kHz deviation
//      MOD_FSK_5_8
//      MOD_GFSK_9_6   — Gaussian-FSK, 9.6 kbps
//      MOD_GFSK_38_4
//      MOD_GFSK_100
//      MOD_MSK_250    — Minimum Shift Keying
//      MOD_MSK_500
//
//  Data rate, channel bandwidth, sync word, and packet length are
//  configured per profile. The skeleton stubs leave every setting at
//  the driver's defaults; real implementations MUST populate the
//  configuration registers documented in TI SWRZ020 (CC1101 datasheet)
//  before the radio is keyed up. Unrestricted transmit power is
//  NEVER shipped — see the comments in hal_radio_send().
//
//  All raw capture is bounded by RADIO_CAPTURE_BYTES (4 KB) so the
//  process RSS never spikes during long captures.

typedef enum {
    RADIO_BAND_315 = 0,
    RADIO_BAND_433 = 1,
    RADIO_BAND_868 = 2,
    RADIO_BAND_915 = 3,
} RadioBand;

typedef enum {
    RADIO_MOD_ASK_OOK  = 0,
    RADIO_MOD_ASK_RAW  = 1,
    RADIO_MOD_FSK_1_2  = 2,
    RADIO_MOD_FSK_5_8  = 3,
    RADIO_MOD_GFSK_9_6 = 4,
    RADIO_MOD_GFSK_38  = 5,
    RADIO_MOD_GFSK_100 = 6,
    RADIO_MOD_MSK_250  = 7,
    RADIO_MOD_MSK_500  = 8,
} RadioModulation;

// One spectrum-analyzer bin. Centre frequency in MHz, RSSI in dBm.
// The bins are returned in ascending frequency order so the UI can
// draw a single sweep line per frame.
typedef struct {
    float    freq_mhz;
    int8_t   rssi_dbm;
    uint8_t  activity;       // 1 if a packet preamble was detected
} RadioSweepBin;

bool hal_radio_set_band(RadioBand b);
RadioBand hal_radio_get_band();
bool hal_radio_set_modulation(RadioModulation m);

// Signal capture & replay ---------------------------------------------------
//
//   hal_radio_capture_start():
//     Idle on the configured frequency and push every received packet into
//     an internal ring of up to RADIO_CAPTURE_BYTES total payload bytes.
//     On overflow, drop the oldest packet. The skeleton returns false
//     because the Linux CC1101 driver is not yet integrated.
//
//   hal_radio_capture_step():
//     Returns one captured packet (raw bytes + frequency + timestamp).
//
//   hal_radio_capture_replay():
//     Re-transmit the most recent capture once. Real implementations MUST
//     check that the operator is physically at the same site as the
//     target; replay is unsafe outside the operator's RF perimeter.
//
//   hal_radio_capture_save_pcap():
//     Dump every captured packet as a libpcap record via hal_sdcard_open.

bool hal_radio_capture_start();
bool hal_radio_capture_step(uint8_t *out, uint8_t *out_len,
                             float *freq_mhz, uint32_t *timestamp_ms);
void hal_radio_capture_stop();
bool hal_radio_capture_replay();
bool hal_radio_capture_save_pcap();

// Frequency spectrum analyzer --------------------------------------------
//
//   hal_radio_sweep_start():
//     Begin a sweep across the configured band, hopping in
//     channel-bandwidth steps. The skeleton returns false.
//
//   hal_radio_sweep_step():
//     Returns the next sweep bin (or false at end of sweep). The caller
//     is expected to schedule a re-sweep every 5..10 seconds.
//
//   hal_radio_spectrum_save_csv():
//     Append the current sweep to /mnt/void-os/rf/spectrum_<UTC>.csv
//     with columns: utc, freq_mhz, rssi_dbm, activity.

bool hal_radio_sweep_start();
bool hal_radio_sweep_step(RadioSweepBin *out);
void hal_radio_sweep_stop();
bool hal_radio_spectrum_save_csv();

// Fixed-code generator ---------------------------------------------------
//
//   Cycles through standard fixed-bit sequences (12 or 24 DIP switch
//   configurations) for the chosen band. The skeleton supports 12-bit
//   and 24-bit encodings used by common gate/barrier remotes:
//
//      * 12-bit:  trinary (0, 1, float) — 3^12 ≈ 531 k codes
//      * 24-bit:  binary — 2^24 ≈ 16 M codes
//
//   Caller controls the bit width via hal_radio_fixed_set_width().
//   Each iteration of hal_radio_fixed_step() emits the next code with
//   a configurable dwell time. NEVER run this against a target without
//   the operator's explicit consent and the appropriate authorisation.

bool hal_radio_fixed_start(uint8_t width_bits, uint32_t dwell_ms);
bool hal_radio_fixed_step(uint64_t *code_out);
void hal_radio_fixed_stop();

// POCSAG decoder ---------------------------------------------------------
//
//   POCSAG is the protocol used by unencrypted legacy pagers. The
//   decoder runs in hal_radio_tick() once hal_radio_pocsag_start() is
//   called; each decoded message is pushed into a small ring buffer and
//   consumed via hal_radio_pocsag_pop().
//
//   Skeleton returns false on every call; real implementation needs:
//     * Manchester decoder over the CC1101 GDO0 pin (rising / falling
//       edge time-stamping).
//     * Sync-codeword detection (0x7CD215D8 for 32-bit batches).
//     * BCH(31,21) error correction.
//     * Numeric / alphanumeric address matching.

typedef struct {
    uint32_t timestamp_ms;
    uint32_t address;
    uint8_t  function;        // 0 = numeric, 3 = alphanumeric
    char     text[80];        // ASCII, NUL-terminated
} RadioPocsagMsg;

bool hal_radio_pocsag_start();
bool hal_radio_pocsag_pop(RadioPocsagMsg *out);
void hal_radio_pocsag_stop();

// ── Raw bit transmission (OOK / f) ────────────────────────────────────────
//
//  hal_radio_send_raw_bits transmits an arbitrary bit stream on the
//  currently configured band using the CC1101 in async serial mode.
//
//  Each "bit" is one µs of carrier on or off; the caller builds a pulse
//  list and the HAL loads it into the CC1101 TX FIFO in async mode so
//  the CPU is free while the bits air.
//
//  Real impl (Pi 5):
//      hal_radio_set_modulation(MOD_ASK_OOK);   // most legacy targets
//      cc1101.strobe(SIDLE);
//      for each on/off pulse:
//          cc1101.write_burst(TXFIFO, &pulse, 1);
//      cc1101.strobe(STX);                       // begin transmission
//      while (!gdo0_raised) {}                   // wait for preamble ack
//
//  Real impl (ESP32): the SmartRC lib supports async TX through
//      SpiWriteBurst(CC1101_REG_0E, ...) plus the strobe sequence.
//
//  LEGAL: same as hal_wifi_inject_frame — operator authorisation
//  required. Refused in stealth mode.
bool hal_radio_send_raw_bits(const uint16_t *pulses_us, uint16_t count);
