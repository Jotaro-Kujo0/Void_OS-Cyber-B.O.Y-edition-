// app_home.h
#pragma once
#include "os/events.h"

void app_home_init();
void app_home_tick();
void app_home_draw();
void app_home_event(Event e);
uint8_t app_home_selected();
