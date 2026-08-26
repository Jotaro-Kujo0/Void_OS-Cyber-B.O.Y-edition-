// app_help.h — slot APP_HELP: built-in cheatsheet + man overlay.
//
// Sub-modes:
//   0. THIS APP    — show the active app's description
//   1. ALL APPS    — short blurb per app + key bindings summary
//   2. HAL KEYS    — editable hal_storage keys reference
//   3. KEY MAP     — button legend for the whole OS

#pragma once
#include "../os/events.h"

void app_help_init();
void app_help_tick();
void app_help_draw();
void app_help_event(Event e);
void app_help_suspend();
