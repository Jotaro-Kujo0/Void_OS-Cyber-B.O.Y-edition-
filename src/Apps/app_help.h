// app_help.h — built-in cheatsheet + man overlay.
// Modes: THIS APP / ALL APPS / HAL KEYS / KEY MAP.

#pragma once
#include "../os/events.h"

void app_help_init();
void app_help_tick();
void app_help_draw();
void app_help_event(Event e);
void app_help_suspend();
