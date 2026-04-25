// sprite.h
#pragma once
#include <stdint.h>

typedef enum : uint8_t {
    ANIM_IDLE=0, ANIM_BLINK, ANIM_HAPPY,
    ANIM_FOCUS,  ANIM_ALERT, ANIM_TIRED,
    ANIM_CURIOUS,ANIM_COUNT
} AnimID;

typedef struct {
    uint8_t start;  // sheet frame index
    uint8_t count;
    uint8_t fps;
    bool    loop;
} AnimClip;

typedef struct {
    AnimID   current, queued;
    uint8_t  frame;
    uint32_t last_ms;
    bool     done;
} SpritePlayer;

void sprite_init   (SpritePlayer *p);
void sprite_play   (SpritePlayer *p, AnimID id, bool queue);
void sprite_tick   (SpritePlayer *p);
void sprite_draw   (SpritePlayer *p, int x, int y);
AnimID sprite_current(const SpritePlayer *p);