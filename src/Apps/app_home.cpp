#include "app_home.h"
#include "app_base.h"
#include "ui/character/character.h"
#include "ui/draw.h"
#include "ui/theme.h"
#include "ui/transition.h"
#include "hal/hal_storage.h"
#include "config.h"
#include <stdio.h>

static Character _char;
static uint8_t   _sel = 1;
static uint8_t   _pot_prev = 255;

static const float EYE[APP_COUNT][2] = {
    { 0.0f, 0.0f},
    {-0.7f,-0.5f}, {-0.7f, 0.1f}, {-0.5f, 0.6f},
    { 0.7f,-0.5f}, { 0.7f, 0.1f}, { 0.5f, 0.4f},
    { 0.0f,-0.7f},
};

static const char* NAMES[APP_COUNT] = {
    "HOME","STAT","MAP","LOG","RADIO","NFC","IR","SYS"
};
static const char* DESCS[APP_COUNT] = {
    "","vitals","navigate","logger","sub-ghz","rfid","infrared","system"
};
static const float FILLS[APP_COUNT] = {
    0,0.87f,0.6f,0.45f,0.72f,0.5f,0.3f,1.0f
};
static const AnimID ANIMS[APP_COUNT] = {
    ANIM_IDLE, ANIM_HAPPY, ANIM_CURIOUS, ANIM_TIRED,
    ANIM_FOCUS, ANIM_FOCUS, ANIM_ALERT, ANIM_IDLE
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

    // Left panel
    const uint8_t LA[] = {1,2,3};
    for (int i=0;i<3;i++)
        draw_panel_row(0, STATS_H+i*ROW_H, PANEL_W, ROW_H,
                       LA[i], _sel==LA[i], false);
    draw_vline(PANEL_W, STATS_H, SCR_H-STATS_H, T_BORDER);

    // Right panel
    const uint8_t RA[] = {4,5,6};
    draw_vline(RIGHT_X-1, STATS_H, SCR_H-STATS_H, T_BORDER);
    for (int i=0;i<3;i++)
        draw_panel_row(RIGHT_X, STATS_H+i*ROW_H, PANEL_W, ROW_H,
                       RA[i], _sel==RA[i], true);

    // Center character
    int sx = CENTER_X + (CENTER_W - SPRITE_W) / 2;
    int sy = STATS_H  + ((SCR_H-STATS_H-SPRITE_H) / 2);
    character_draw(&_char, sx, sy);

    // Bottom hint
    draw_hline(0, SCR_H-16, SCR_W, T_BORDER);
    draw_text(CENTER_X, SCR_H-12, "[A] ENTER  [B] ---  [C] SYS",
              T_DIM, T_BG, FONT_SM);
}

uint8_t app_home_selected() { return _sel; }