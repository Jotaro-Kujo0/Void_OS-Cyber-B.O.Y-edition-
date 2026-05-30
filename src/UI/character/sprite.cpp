// sprite.cpp - JPG Asset Loading System
#include "sprite.h"
#include "../draw.h"
#include "config.h"
#include <Arduino.h>

// JPG asset filenames stored in /UI/assets/
static const char* JPG_ASSETS[] = {
    "/assets/IDLE.jpg",    // ANIM_IDLE
    "/assets/IDLE.jpg",    // ANIM_BLINK (reuse IDLE for blink frames)
    "/assets/IDLE.jpg",    // ANIM_HAPPY
    "/assets/IDLE.jpg",    // ANIM_FOCUS
    "/assets/IDLE.jpg",    // ANIM_ALERT
    "/assets/IDLE.jpg",    // ANIM_TIRED
    "/assets/IDLE.jpg",    // ANIM_CURIOUS
};

// Animation frame timings (for frame sequencing)
static const AnimClip CLIPS[ANIM_COUNT] = {
    {0,  1, 8,  true },   // IDLE    - single frame, looping
    {0,  1, 18, false},   // BLINK   - single frame, no loop
    {0,  1, 10, false},   // HAPPY   - single frame
    {0,  1, 10, false},   // FOCUS   - single frame
    {0,  1, 12, true },   // ALERT   - single frame, looping
    {0,  1, 5,  true },   // TIRED   - single frame, looping
    {0,  1, 10, true },   // CURIOUS - single frame, looping
};

void sprite_init(SpritePlayer *p) {
    p->current = p->queued = ANIM_IDLE;
    p->frame   = 0;
    p->last_ms = 0;
    p->done    = false;
}

void sprite_play(SpritePlayer *p, AnimID id, bool queue) {
    if (queue && !p->done) { p->queued = id; return; }
    p->current = id;
    p->frame   = 0;
    p->done    = false;
    p->last_ms = millis();
}

void sprite_tick(SpritePlayer *p) {
    const AnimClip &c = CLIPS[p->current];
    if (millis() - p->last_ms < (uint32_t)(1000 / c.fps)) return;
    p->last_ms = millis();
    p->frame++;
    if (p->frame >= c.count) {
        p->frame = c.loop ? 0 : c.count - 1;
        p->done  = !c.loop;
        if (p->done && p->queued != p->current)
            sprite_play(p, p->queued, false);
    }
}

void sprite_draw(SpritePlayer *p, int dx, int dy) {
    // Load and draw JPG asset for current animation
    const char *asset_path = JPG_ASSETS[p->current];
    
    // Render JPG from SPIFFS /assets/ directory
    draw_jpg(dx, dy, asset_path);
}

AnimID sprite_current(const SpritePlayer *p) { return p->current; }
