#include "character.h"
#include "../draw.h"
#include "../../config.h"
#include <string.h>
#include <stdio.h>

// Asset mappings: app_id -> head image path
// Using .jpg files from src/UI/assets/
static const char *HEAD_IMAGES[APP_COUNT] = {
    "UI/assets/IDLE.jpg",       // APP_HOME = 0
    "UI/assets/STAT.jpg",       // APP_STAT = 1
    "UI/assets/MAP.jpg",        // APP_MAP = 2
    "UI/assets/DATA.jpg",       // APP_LOG = 3 (using DATA)
    "UI/assets/RAD-O.jpg",      // APP_RADIO = 4
    "UI/assets/Marauder.jpg",   // APP_NFC = 5
    "UI/assets/MISC.jpg",       // APP_IR = 6
    "UI/assets/STAT.jpg",       // APP_SYS = 7 (using STAT)
};

// Animation -> animation info map (frame count, delay)
static const struct {
    uint8_t  frame_count;
    uint8_t  frame_delay;  // ms per frame
} ANIM_INFO[ANIM_COUNT] = {
    {1, 0},      // IDLE - static
    {1, 0},      // HAPPY - static
    {1, 0},      // CURIOUS - static
    {1, 0},      // TIRED - static
    {1, 0},      // FOCUS - static
    {1, 0},      // ALERT - static
};

void character_init(Character *c) {
    c->app_id = 0;
    c->anim_id = ANIM_IDLE;
    c->target_x = 0.0f;
    c->target_y = 0.0f;
    c->cur_x = 0.0f;
    c->cur_y = 0.0f;
    c->frame = 0;
    c->head_img = HEAD_IMAGES[0];
}

void character_tick(Character *c) {
    // Smooth movement towards target position
    float smooth_factor = 0.1f;
    c->cur_x += (c->target_x - c->cur_x) * smooth_factor;
    c->cur_y += (c->target_y - c->cur_y) * smooth_factor;
    c->frame++;
}

void character_draw(Character *c) {
    // Draw body.jpg at center
    // Center of screen is SCR_W/2, SCR_H/2
    // Body is ~100x160 px, center it
    int body_x = (SCR_W / 2) - 50;
    int body_y = (SCR_H / 2) - 80;
    draw_jpg(body_x, body_y, "UI/assets/BODY.jpg");

    // Draw head.jpg with position offset
    // Head is ~60x80 px, positioned above body
    int head_off_x = (int)(c->cur_x * 20);  // scale offset
    int head_off_y = (int)(c->cur_y * 20);
    int head_x = body_x + 25 + head_off_x;
    int head_y = body_y - 60 + head_off_y;
    if (c->head_img) {
        draw_jpg(head_x, head_y, c->head_img);
    }
}

void character_set_app(Character *c, uint8_t app_id) {
    if (app_id >= APP_COUNT) app_id = 0;
    c->app_id = app_id;
    c->head_img = HEAD_IMAGES[app_id];

    // Map app to animation (from app_home.cpp ANIMS array)
    static const AnimID ANIMS[APP_COUNT] = {
        ANIM_IDLE, ANIM_HAPPY, ANIM_CURIOUS, ANIM_TIRED,
        ANIM_FOCUS, ANIM_FOCUS, ANIM_ALERT, ANIM_IDLE
    };
    character_set_anim(c, ANIMS[app_id]);
}

void character_set_anim(Character *c, AnimID anim) {
    if (anim >= ANIM_COUNT) anim = ANIM_IDLE;
    c->anim_id = anim;
    c->frame = 0;
}
