#pragma once
#include <stdint.h>

// ── Screen ────────────────────────────────────────────────────────────────
#define SCR_W           480
#define SCR_H           320
#define STATS_H          28    // top bar height
#define PANEL_W         186    // left/right app panel width
#define CENTER_X        186    // character panel left edge
#define CENTER_W        108    // character panel width
#define RIGHT_X         294    // right panel left edge

// ── GPIO ─────────────────────────────────────────────────────────────────
#define PIN_POT          4
#define PIN_BTN_A       38
#define PIN_BTN_B       39
#define PIN_BTN_C       40
#define PIN_BL           6     // backlight PWM
#define PIN_CC1101_CS   15
#define PIN_CC1101_GDO0 16
#define PIN_PN532_CS     5
#define PIN_IR_RX       41
#define PIN_IR_TX       42
#define PIN_ONEWIRE      2
// SPI2 shared bus (CC1101 + PN532)
#define PIN_SPI2_MOSI   35
#define PIN_SPI2_MISO   36
#define PIN_SPI2_SCK    37

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