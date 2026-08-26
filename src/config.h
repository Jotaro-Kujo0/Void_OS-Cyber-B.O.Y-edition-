#pragma once
#include <stdint.h>

// ── Screen ────────────────────────────────────────────────────────────────
// ILI9341 240x320. The UI uses portrait coordinates.
#define SCR_W           240
#define SCR_H           320
#define STATS_H          28
#define PANEL_W          72
#define CENTER_X         72
#define CENTER_W         96
#define RIGHT_X          168

// ── GPIO ─────────────────────────────────────────────────────────────────
// ── GPIO expansion: extended feature pinout ───────────────────────────
//
//  The following pins are reserved by the extended feature set (sub-GHz
//  auditing, NFC auditing, Wi-Fi/BLE, IR array, HID, bus sniffing, stealth
//  hardware). They are intentionally picked outside the SPI/I2C/UART pins
//  already used so that the existing buses are not re-mapped when these new
//  features are enabled.
//
//  Raspberry Pi 5 free BCM pins (after the SPI / I2C / TFT pins above):
//      5, 8, 9, 10, 11, 13, 14, 15, 17, 19, 20, 21, 26, 27
//  ESP32 free pins (not in boot-strapping mode):
//      14, 16, 17, 25 (and a few others; see hal_pins.h notes)
//
//  Each pin belongs to one of two MOSFET-controlled loads:
//    * PIN_HAPTIC  – coin vibration motor through IRLZ44N #1
//    * PIN_IR_ARRAY – 5V 940nm IR LED array through IRLZ44N #2
//  and to one of three direct GPIO loads:
//    * PIN_RF_KILL      – hardware RF kill (cuts CC1101 VCC)
//    * PIN_WIEGAND_D0/D1 – physical access-control reader D0/D1 data lines
//    * PIN_LOGIC_*      – 4-channel software logic analyzer inputs
#ifdef VOIDOS_RPI5
// Raspberry Pi 5 uses BCM GPIO numbers. SPI0 CE0 is controlled by spidev.
#define PIN_POT          0     // external ADC IIO channel 0; Pi GPIO has no ADC
#define PIN_BTN_A        0     // buttons are on PCF8574 P0/P1/P2 over I2C-1
#define PIN_BTN_B        0
#define PIN_BTN_C        0
#define PIN_BL           18    // optional hardware PWM backlight
#define PIN_CC1101_CS    7     // reserved for the future Linux CC1101 driver
#define PIN_CC1101_GDO0  16
#define PIN_PN532_IRQ    6
#define PIN_IR_RX        23    // use /dev/lirc0 for production IR support
#define PIN_IR_TX        12
#define PIN_ONEWIRE      4
#define PIN_TFT_DC       25
#define PIN_TFT_RST      24
#define PIN_I2C_SDA      2
#define PIN_I2C_SCL      3
// ── Extended pins (Pi 5) ──
#define PIN_HAPTIC       13    // MOSFET gate for haptic vibration motor
#define PIN_IR_ARRAY     14    // MOSFET gate for 940nm IR LED array
#define PIN_RF_KILL      17    // MOSFET gate for CC1101 RF isolation (stealth)
#define PIN_WIEGAND_D0   20    // Wiegand reader D0 data line
#define PIN_WIEGAND_D1   21    // Wiegand reader D1 data line
#define PIN_LOGIC_CH0     5    // logic analyzer channel 0
#define PIN_LOGIC_CH1     8    // logic analyzer channel 1
#define PIN_LOGIC_CH2     9    // logic analyzer channel 2
#define PIN_LOGIC_CH3    10    // logic analyzer channel 3
#define PIN_LOGIC_TRIG   11    // logic analyzer trigger (rising edge)
#else
// ESP32-WROOM-32E wiring.
#define PIN_POT          34
#define PIN_BTN_A        36
#define PIN_BTN_B        39
#define PIN_BTN_C        32
#define PIN_BL           13
#define PIN_CC1101_CS    15
#define PIN_CC1101_GDO0  26
#define PIN_PN532_IRQ    5
#define PIN_IR_RX        35
#define PIN_IR_TX        33
#define PIN_ONEWIRE      2
#define PIN_TFT_DC       27
#define PIN_TFT_RST      4
#define PIN_I2C_SDA     21
#define PIN_I2C_SCL     22
// ── Extended pins (ESP32) ──
#define PIN_HAPTIC       14    // MOSFET gate for haptic vibration motor
#define PIN_IR_ARRAY     16    // MOSFET gate for 940nm IR LED array
#define PIN_RF_KILL      17    // MOSFET gate for CC1101 RF isolation (stealth)
#define PIN_WIEGAND_D0   25    // Wiegand reader D0 data line
#define PIN_WIEGAND_D1   26    // Wiegand reader D1 data line
#define PIN_LOGIC_CH0    32    // logic analyzer channel 0
#define PIN_LOGIC_CH1    33    // logic analyzer channel 1
#define PIN_LOGIC_CH2    34    // logic analyzer channel 2 (input-only on ESP32)
#define PIN_LOGIC_CH3    35    // logic analyzer channel 3 (input-only on ESP32)
#define PIN_LOGIC_TRIG   27    // logic analyzer trigger
#endif

// SPI pins are selected by the board peripheral: SPI0 on Raspberry Pi 5,
// SPI1 GPIO18/19/23 on the original ESP32 wiring.
#define PIN_SD_CS        25

// ── Timing ────────────────────────────────────────────────────────────────
#define FPS              30
#define FRAME_MS        (1000 / FPS)
#define DIM_TIMEOUT_MS  30000
#define SLEEP_TIMEOUT_MS 120000

// ── Power ────────────────────────────────────────────────────────────────
#define BL_FULL         220
#define BL_DIM           40

// ── Apps ─────────────────────────────────────────────────────────────────
#define APP_HOME         0
#define APP_STAT         1
#define APP_WIFI         2   // formerly APP_MAP (null stub) – Wi-Fi/BLE audit
#define APP_LOG          3   // field logger / PCAP (was null stub, now live)
#define APP_RADIO        4
#define APP_NFC          5
#define APP_IR           6
#define APP_SYS          7
#define APP_BUS          8   // bus sniffer / Wiegand / iButton
#define APP_HID          9   // BLE keyboard / USB injection
#define APP_DARK        10   // stealth / dark-mode settings hub
#define APP_SCAN        11   // ARP / TCP port scan + service hint
#define APP_ROGUE       12   // rogue AP + captive portal + DNS spoof
#define APP_SNIFF       13   // HTTP URL logger + cookie extractor + MITM
#define APP_FUZZ        14   // HTTP fast fuzzer
#define APP_WEB         15   // HTTP web scraper (URL / GET / PARSE / LINKS)
#define APP_BODY        16   // Wi-Fi body & presence tracker (probe ring)
#define APP_HELP        17   // cheatsheet + per-app man
#define APP_DROP        18   // USB drop attack generator
#define APP_QR          19   // QR-code generator
#define APP_COUNT       20

// ── Persistent storage keys ──────────────────────────────────────────────
#define NVS_NS          "cyberdeck"
#define NVS_BL_KEY      "bl_bright"
#define NVS_LAST_APP    "last_app"

// ── Colors (RGB565) ──────────────────────────────────────────────────────
#define C_BLACK         0x0000
#define C_WHITE         0xFFFF
#define C_PGREEN        0x27E4
#define C_DGREEN        0x0320
#define C_MGRAY         0x0820
#define C_BORDER        0x0440
#define C_RED           0xF800
#define C_AMBER         0xFC00
#define C_BLUE          0x001F
#define TRANSPARENT     0xF81F

// ── Extended feature limits ───────────────────────────────────────────────
//
//  These limits exist to bound heap usage on the 4 GB Pi 5 target. Anything
//  that grows at runtime (capture buffers, tag history, GPX/CSV logs) is
//  capped by a *_MAX symbol here. Adjust upward only after measuring free
//  RAM with `cat /proc/<pid>/status | grep -i vmrss` while the feature is
//  active.
#define RADIO_CAPTURE_BYTES   4096   // max raw CC1101 RX capture buffer
#define IR_CAPTURE_PULSES     1024   // max IR pulse-width samples
#define NFC_TAG_LOG           32    // max NDEF/UID log entries kept in RAM
#define WIFI_AP_LOG           32    // max wardriving AP entries kept in RAM
#define BLE_DEVICE_LOG        16    // max BLE devices kept in RAM
#define GATT_HANDLE_LOG       32    // max GATT characteristic handles
#define LOGIC_SAMPLE_DEPTH    4096  // per-channel logic-analyzer samples
#define LOGIC_CHANNELS        4     // mirrors PIN_LOGIC_CH0..CH3
#define WIEGAND_MAX_BITS      64    // Wiegand standard: 26/34/37/42/44 bits
#define IBUTTON_ROM_BYTES     8     // DS1990A is 8 bytes (64-bit ROM)
#define DARK_MODE_HAPTIC_MS   60    // default haptic pulse length
#define HEATMAP_POINT_LIMIT   256   // GPX/CSV heatmap ring buffer
#define PCAP_SNAP_LEN         256   // truncated link-layer snapshot length
#define WORDLIST_PATH_MAX     64    // path string length for SD wordlists

// ── Sprite frame dimensions ──────────────────────────────────────────────
#define SPRITE_W         96
#define SPRITE_H         96
#define SPRITE_COLS       3
