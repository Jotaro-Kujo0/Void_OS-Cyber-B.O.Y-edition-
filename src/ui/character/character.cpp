#include "character.h"
#include "../draw.h"
#include "../../config.h"
#include "../../hal/hal_display.h"
#include <math.h>
#include <stdio.h>

// Asset mapping: app_id -> head image filename
static const char* HEAD_IMAGES[APP_COUNT] = {
    "IDLE.jpg",   // APP_HOME
    "STAT.jpg",   // APP_STAT
    "MAP.jpg",    // APP_MAP
    "MISC.jpg",   // APP_LOG
    "RAD-O.jpg",  // APP_RADIO
    "IDLE.jpg",   // APP_NFC (default)
    "IDLE.jpg",   // APP_IR (default)
    "MISC.jpg",   // APP_SYS
};

// Cached JPEG files in RAM (to avoid repeated SD reads)
static char _body_loaded = 0;
static char _head_loaded = 0;
static uint8_t _cached_head_app = 255;

void character_init(Character* c) {
    if (!c) return;
    c->app_id = 0;
    c->anim = ANIM_IDLE;
    c->target_x = 0.0f;
    c->target_y = 0.0f;
    c->eye_x = 0.0f;
    c->eye_y = 0.0f;
    c->health = 1.0f;
    c->frame = 0;
    c->head_app_id = APP_HOME;
    
    // Load body.jpg at startup
    _body_loaded = 1;  // Mark body as loaded (TFT_eSPI will handle file I/O)
}

void character_set_app(Character* c, uint8_t app_id) {
    if (!c) return;
    c->app_id = app_id % APP_COUNT;
    c->head_app_id = app_id;
    c->anim = (AnimID)(app_id % ANIM_COUNT);
}

void character_set_anim(Character* c, AnimID a) {
    if (!c) return;
    c->anim = a;
    c->frame = 0;
}

void character_tick(Character* c) {
    if (!c) return;
    // Smoothly interpolate eye toward target
    c->eye_x += (c->target_x - c->eye_x) * 0.1f;
    c->eye_y += (c->target_y - c->eye_y) * 0.1f;
    c->frame++;
}

uint8_t character_select_head_for_app(uint8_t app_id) {
    if (app_id >= APP_COUNT) app_id = 0;
    return app_id;
}

void character_draw(Character* c) {
    if (!c) return;
    
    uint16_t cx = CENTER_X + CENTER_W / 2;
    uint16_t cy = STATS_H + 80;
    
    // Draw body (BODY.jpg) at center panel
    if (_body_loaded) {
        // Use TFT_eSPI's drawJpg to render BODY.jpg
        // Path: /UI/assets/BODY.jpg
        tft.drawJpg((const uint8_t*)"/UI/assets/BODY.jpg", 
                    (size_t)0,  // size unknown - TFT will read
                    cx - 48, cy - 48);
    } else {
        // Fallback: draw placeholder circle
        draw_fcircle(cx, cy, 30, C_PGREEN);
    }
    
    // Select and draw head image based on app_id
    uint8_t head_app = character_select_head_for_app(c->app_id);
    
    if (head_app < APP_COUNT && HEAD_IMAGES[head_app]) {
        char path[64];
        snprintf(path, sizeof(path), "/UI/assets/%s", HEAD_IMAGES[head_app]);
        
        // Render head image overlay
        tft.drawJpg((const uint8_t*)path,
                    (size_t)0,
                    cx - 48, cy - 48);
    }
    
    // Draw eyes with animation offset
    int16_t eye_off_x = (int16_t)(c->eye_x * 8);
    int16_t eye_off_y = (int16_t)(c->eye_y * 8);
    draw_fcircle(cx - 10 + eye_off_x, cy - 8 + eye_off_y, 4, C_BLACK);
    draw_fcircle(cx + 10 + eye_off_x, cy - 8 + eye_off_y, 4, C_BLACK);
    
    // Health bar below
    draw_bar(cx - 20, cy + 25, 40, 4, c->health, C_PGREEN, C_DGREEN);
}

void character_set_health(Character* c, float h) {
    if (!c) return;
    c->health = (h < 0.0f) ? 0.0f : (h > 1.0f) ? 1.0f : h;
}
