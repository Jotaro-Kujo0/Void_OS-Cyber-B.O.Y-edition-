#pragma once
#include <stdint.h>

// ── Screen ────────────────────────────────────────────────────────────────
// BOM: ILI9341 240x320 (not 480x320)
#define SCR_W           240
#define SCR_H           320
#define STATS_H          28    // top bar height
#define PANEL_W          72    // left/right app panel width (240 - 2*72 = 96 center)
#define CENTER_X         72    // character panel left edge
#define CENTER_W         96    // character panel width
#define RIGHT_X         168    // right panel left edge

// ── GPIO ─────────────────────────────────────────────────────────────────
// BOM: ESP32-WROOM-32E standard pins (matches platformio.ini)
#define PIN_POT          34    // Potentiometer analog input
#define PIN_BTN_A        36    // Button A
#define PIN_BTN_B        39    // Button B
#define PIN_BTN_C        32    // Button C
#define PIN_BL           13    // Backlight PWM
#define PIN_CC1101_CS    15    // CC1101 Chip Select
#define PIN_CC1101_GDO0  26    // CC1101 GDO0 (interrupt)
#define PIN_PN532_IRQ     5    // PN532 Interrupt (I2C mode doesn't need CS)
#define PIN_IR_RX        35    // IR receiver pin
#define PIN_IR_TX        33    // IR transmitter (LED control) pin
#define PIN_ONEWIRE       2    // DS18B20 temperature sensor

// SPI1 shared bus (TFT + CC1101 + SD Card)
// Pins defined in platformio.ini build_flags for TFT:
// SCLK=18, MOSI=23, MISO=19, TFT_CS=14, TFT_DC=27, TFT_RST=4
// SD Card: CS on pin 25
#define PIN_SD_CS        25    // SD card Chip Select

// ── Timing ────────────────────────────────────────────────────────────────
#define FPS              30
#define FRAME_MS        (1000 / FPS)
#define DIM_TIMEOUT_MS  30000   // 30s → dim backlight
#define SLEEP_TIMEOUT_MS 120000 // 2min → deep sleep

// ── Power ────────────────────────────────────────────────────────────────
#define BL_FULL         220     // 0–255 PWM
#define BL_DIM           40

// ── Apps ─────────────────────────────────────────────────────────────────
#define APP_HOME         0
#define APP_STAT         1
#define APP_MAP          2
#define APP_LOG          3
#define APP_RADIO        4
#define APP_NFC          5
#define APP_IR           6
#define APP_SYS          7
#define APP_COUNT        8

// ── NVS storage keys ─────────────────────────────────────────────────────
#define NVS_NS          "cyberdeck"
#define NVS_BL_KEY      "bl_bright"
#define NVS_LAST_APP    "last_app"

// ── Colors (RGB565) ──────────────────────────────────────────────────────
#define C_BLACK         0x0000
#define C_WHITE         0xFFFF
#define C_PGREEN        0x27E4   // phosphor green  #39FF14
#define C_DGREEN        0x0320   // dim green       #063400
#define C_MGRAY         0x0820   // panel bg
#define C_BORDER        0x0440   // border lines
#define C_RED           0xF800
#define C_AMBER         0xFC00
#define C_BLUE          0x001F
#define TRANSPARENT     0xF81F   // magenta = sprite transparency key

// ── Sprite frame dimensions ──────────────────────────────────────────────
#define SPRITE_W         96
#define SPRITE_H         96
#define SPRITE_COLS       3