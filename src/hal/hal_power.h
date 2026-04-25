// hal_power.h
#pragma once
#include <stdint.h>

void hal_power_init();
void hal_power_activity();     // call on any input event
void hal_power_tick();         // call each frame
bool hal_power_is_dimmed();