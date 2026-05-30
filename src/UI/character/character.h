#pragma once
#include <stdint.h>

typedef enum {
    ANIM_IDLE,
    ANIM_HAPPY,
    ANIM_CURIOUS,
    ANIM_TIRED,
    ANIM_FOCUS,
    ANIM_ALERT,
    ANIM_COUNT
} AnimID;

typedef struct {
    uint8_t app_id;       // current app (0-7)
    uint8_t anim_id;      // current animation
    float target_x;       // target eye position offset (-1.0 to 1.0)
    float target_y;       // target eye position offset (-1.0 to 1.0)
    float cur_x;          // current eye position offset
    float cur_y;          // current eye position offset
    uint32_t frame;       // frame counter for animation
    const char *head_img; // current head image path
} Character;

// Initialize character with IDLE anim at app 0 (HOME)
void character_init(Character *c);

// Update character state each frame (30 Hz)
void character_tick(Character *c);

// Draw character to screen (body + head)
void character_draw(Character *c);

// Set app context (changes head image and animation)
void character_set_app(Character *c, uint8_t app_id);

// Set animation directly
void character_set_anim(Character *c, AnimID anim);
