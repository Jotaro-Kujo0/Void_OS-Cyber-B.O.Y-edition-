// sprite.cpp
#include "sprite.h"
#include "ui/draw.h"
#include "config.h"
#include <Arduino.h>

// ── Include the generated sprite sheet ────────────────────────────────────
// Run tools/png_to_c.py to regenerate from your art:
//   python tools/png_to_c.py art/sheet.png src/ui/character/sprite_data.h char_sheet
#include "sprite_data.h"   // defines: char_sheet[], CHAR_SHEET_W, CHAR_SHEET_H

static const AnimClip CLIPS[ANIM_COUNT] = {
    {0,  3, 8,  true },   // IDLE    frames 0-2
    {3,  3, 18, false},   // BLINK   frames 3-5  fast, no loop
    {6,  2, 10, false},   // HAPPY   frames 6-7
    {8,  2, 10, false},   // FOCUS   frames 8-9
    {10, 3, 12, true },   // ALERT   frames 10-12 loop
    {13, 2, 5,  true },   // TIRED   frames 13-14 slow
    {15, 3, 10, true },   // CURIOUS frames 15-17
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
    uint8_t sheet_frame = CLIPS[p->current].start + p->frame;
    int sx = (sheet_frame % SPRITE_COLS) * SPRITE_W;
    int sy = (sheet_frame / SPRITE_COLS) * SPRITE_H;
    draw_sprite(dx, dy, char_sheet,
                sx, sy, SPRITE_W, SPRITE_H,
                CHAR_SHEET_W, TRANSPARENT);
}

AnimID sprite_current(const SpritePlayer *p) { return p->current; }