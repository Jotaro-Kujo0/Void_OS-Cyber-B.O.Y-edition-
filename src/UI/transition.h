// transition.h
#pragma once
#include <stdint.h>

typedef enum : uint8_t {
    TRANS_NONE,
    TRANS_SLIDE_LEFT,
    TRANS_SLIDE_RIGHT,
    TRANS_FADE,
} TransType;

// Capture old framebuffer, animate it out.
void transition_start(TransType t);
bool transition_running();
void transition_tick();