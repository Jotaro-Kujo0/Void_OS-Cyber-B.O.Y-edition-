#include "app_home.h"
#include "app_base.h"
#include "UI/character/character.h"
#include "UI/draw.h"
#include "UI/theme.h"
#include "UI/transition.h"
#include "hal/hal_storage.h"
#include "os/scheduler.h"
#include "config.h"
#include <stdio.h>

static Character _char;
static uint8_t   _sel = 1;

static const float EYE[APP_COUNT][2] = {
    { 0.0f,  0.0f},                                 //  0 HOME
    {-0.7f, -0.65f}, {-0.7f, 0.05f}, {-0.7f, 0.55f}, //  1 STAT,  2 WIFI,  3 LOG
    {-0.7f, 0.85f}, {-0.7f, -0.30f},                //  8 BUS,   11 SCAN
    {-0.5f,  0.7f}, {-0.5f, -0.95f},                // 13 SNIFF, 15 WEB (bottom-left well)
    {-0.3f,  0.95f},                                // 17 HELP  (sliver above bottom-left)
    { 0.7f, -0.65f}, { 0.7f, 0.05f}, { 0.7f, 0.55f}, //  4 RADIO,  5 NFC,  6 IR
    { 0.7f,  0.85f}, { 0.7f, -0.30f},               //  9 HID,   12 ROGUE
    { 0.5f,  0.7f}, { 0.5f, -0.95f},                // 14 FUZZ,  16 BODY (bottom-right well)
    { 0.3f,  0.95f},                                // 18 DROP  (sliver above bottom-right)
    {-0.45f,-0.45f},                                //  7 SYS
    { 0.45f,-0.45f},                                // 10 DARK
    { 0.0f,  -0.95f},                               // 19 QR   (centre bottom)
};

static const char* NAMES[APP_COUNT] = {
    "HOME","STAT","WIFI","LOG","RADIO","NFC","IR","SYS","BUS","HID","DARK",
    "SCAN","ROGUE","SNIFF","FUZZ","WEB","BODY","HELP","DROP","QR"
};
static const char* DESCS[APP_COUNT] = {
    "","vitals","wifi/ble","logger","sub-ghz","rfid","infrared","system",
    "cable","badble","stealth",
    "recon","evilap","urllog","fuzzer","scrape","bodies",
    "cheat","usb","gen"
};
static const float FILLS[APP_COUNT] = {
    0,0.87f,0.6f,0.45f,0.72f,0.5f,0.3f,1.0f,
    0.4f,0.55f,0.2f,0.6f,0.7f,0.5f,0.6f,0.5f,
    0.5f,0.8f,0.4f,0.7f
};
static const AnimID ANIMS[APP_COUNT] = {
    ANIM_IDLE, ANIM_HAPPY, ANIM_CURIOUS, ANIM_TIRED,
    ANIM_FOCUS, ANIM_FOCUS, ANIM_ALERT, ANIM_IDLE,
    ANIM_FOCUS, ANIM_ALERT, ANIM_TIRED, ANIM_CURIOUS,
    ANIM_ALERT, ANIM_FOCUS, ANIM_TIRED, ANIM_CURIOUS,
    ANIM_FOCUS, ANIM_HAPPY, ANIM_ALERT, ANIM_CURIOUS
};

static uint8_t pot_to_app(uint8_t p) {
    uint8_t i = (p * (APP_COUNT - 1)) / 256;
    return (i >= APP_COUNT - 1) ? APP_COUNT - 2 : i + 1;
}

void app_home_init() {
    character_init(&_char);
    _sel = hal_storage_get_u8(NVS_LAST_APP, 1);
    if (_sel == 0 || _sel >= APP_COUNT) _sel = 1;
    character_set_app(&_char, _sel);
    _char.target_x = EYE[_sel][0];
    _char.target_y = EYE[_sel][1];
}

void app_home_tick() {
    character_tick(&_char);
}

void app_home_event(Event e) {
    if (e.type == EVT_POT_CHANGED) {
        uint8_t na = pot_to_app(e.data);
        if (na != _sel) {
            _sel = na;
            character_set_app(&_char, _sel);
            _char.target_x = EYE[_sel][0];
            _char.target_y = EYE[_sel][1];
        }
    }
    if (e.type == EVT_BTN_A_DOWN)
        hal_storage_set_u8(NVS_LAST_APP, _sel);
}

static void draw_panel_row(int x, int y, int w, int h,
                            uint8_t aid, bool sel, bool right) {
    if (sel) {
        draw_fill(x+1, y+1, w-2, h-2, T_SEL_BG);
        draw_rect(x+1, y+1, w-2, h-2, T_FG);
    }
    int tx = right ? x+6 : x+6;
    draw_textf(tx, y+6,  sel?T_FG:T_DIM, sel?T_SEL_BG:T_BG, FONT_SM, "%s", NAMES[aid]);
    draw_textf(tx, y+18, sel?0x07C0:0x0180, sel?T_SEL_BG:T_BG, FONT_SM, "%s", DESCS[aid]);
    draw_bar(tx, y+30, w-14, 3, FILLS[aid],
             sel ? T_FG : T_BORDER, T_BG);
}

static void draw_statsbar() {
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_textf(8, 8, T_DIM, T_PANEL, FONT_SM, "CYBERDECK");
    draw_textf(90, 8, T_FG, T_PANEL, FONT_SM, "%s", NAMES[_sel]);
    draw_textf(SCR_W-100, 8, T_DIM, T_PANEL, FONT_SM,
               "BAT:87%%  CPU:%d%%", sched_load());
}

void app_home_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_statsbar();

    // 8 rows so the enlarged catalogue fits in the visible grid (16
    // tile positions + 2 strip slots). Tile row height drops to 30 px
    // so the SYS strip and the bottom hint both fit.
    // Left panel:  STAT(1)  WIFI(2)  LOG(3)  BUS(8)  SCAN(11)  SNIFF(13)  WEB(15)  HELP(17)
    const uint8_t LA[] = {1, 2, 3, 8, 11, 13, 15, 17};
    // Right panel: RADIO(4) NFC(5) IR(6)  HID(9)  ROGUE(12) FUZZ(14)  BODY(16)  DROP(18)
    const uint8_t RA[] = {4, 5, 6, 9, 12, 14, 16, 18};
    // Strip: SYS(7), DARK(10), and QR(19) share the bottom strip with
    // the SYS+DARK combo taking the top and QR dropping to the right.
    const int PANEL_ROWS = 8;
    const int row_h = 30;
    for (int i = 0; i < PANEL_ROWS; ++i)
        draw_panel_row(0, STATS_H + i * row_h, PANEL_W, row_h,
                       LA[i], _sel == LA[i], false);
    draw_vline(PANEL_W, STATS_H, SCR_H - STATS_H, T_BORDER);
    draw_vline(RIGHT_X - 1, STATS_H, SCR_H - STATS_H, T_BORDER);
    for (int i = 0; i < PANEL_ROWS; ++i)
        draw_panel_row(RIGHT_X, STATS_H + i * row_h, PANEL_W, row_h,
                       RA[i], _sel == RA[i], true);

    // SYS row (7) — full-width strip just above the hint.
    int y = STATS_H + PANEL_ROWS * row_h;
    draw_fill(0, y, SCR_W, 24, T_SEL_BG);
    // Highlight row using the focused tile boundary.
    const bool dark_focused = _sel == APP_DARK;
    draw_rect(0, y, SCR_W, 24, _sel == 7 ? T_FG : dark_focused ? T_FG : T_BORDER);
    draw_textf(8, y + 6, _sel == 7 ? T_FG : T_DIM, T_SEL_BG, FONT_SM,
               "%s %s", _sel == 7 ? ">" : " ",
               NAMES[APP_SYS]);
    draw_textf(SCR_W - 100, y + 6, _sel == 7 ? T_FG : T_DIM, T_SEL_BG,
               FONT_SM, "%s", DESCS[APP_SYS]);
    // DARK (10) sits on the right side of the SYS strip.
    draw_textf(SCR_W - 40, y + 6, dark_focused ? T_FG : T_DIM,
               T_SEL_BG, FONT_SM,
               dark_focused ? ">DARK" : "DARK");

    // Center OC
    character_draw(&_char);

    // Bottom hint
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(CENTER_X, SCR_H - 12, "[A] ENTER  [B] ---  [C] SYS",
              T_DIM, T_BG, FONT_SM);
}

uint8_t app_home_selected() { return _sel; }