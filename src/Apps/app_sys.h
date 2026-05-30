// app_sys.h
#pragma once
#include "os/events.h"

void app_sys_init();
void app_sys_tick();
void app_sys_draw();
void app_sys_event(Event e);
