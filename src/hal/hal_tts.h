// hal_tts.h — text-to-speech SKELETON.
//
// Wraps `espeak-ng`/SAPI/TTS via the platform's audio output. Used by
// the help overlay and the in-app notifications overlay.
#pragma once
#include <stdint.h>
#include <stdbool.h>

void hal_tts_say(const char *text);
