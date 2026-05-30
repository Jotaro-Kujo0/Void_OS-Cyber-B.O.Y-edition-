#pragma once
#include <stdint.h>

typedef enum : uint8_t {
    ANIM_IDLE    = 0,
    ANIM_HAPPY   = 1,
    ANIM_CURIOUS = 2,
    ANIM_TIRED   = 3,
    ANIM_FOCUS   = 4,
    ANIM_ALERT   = 5,
    ANIM_COUNT   = 6
} AnimID;

struct Character {
    uint8_t app_id;          // Current app displayed
    AnimID anim;             // Current animation state
    float target_x;          // Target eye position X
    float target_y;          // Target eye position Y
    float eye_x;             // Current eye position X
    float eye_y;             // Current eye position Y
    float health;            // Health bar fill 0-1
    uint16_t frame;          // Animation frame counter
    uint8_t head_app_id;     // App ID for head image selection
};

// Initialize character system — load BODY.jpg at startup
void character_init(Character* c);

// Set active app (affects head image and animation)
void character_set_app(Character* c, uint8_t app_id);

// Set animation state
void character_set_anim(Character* c, AnimID a);

// Update character state (call once per frame)
void character_tick(Character* c);

// Render character to display (center panel) — body + head
void character_draw(Character* c);

// Set health/fill bar value (0-1)
void character_set_health(Character* c, float h);

// Select and load head image for given app
// Returns app ID used to determine head image file
uint8_t character_select_head_for_app(uint8_t app_id);
