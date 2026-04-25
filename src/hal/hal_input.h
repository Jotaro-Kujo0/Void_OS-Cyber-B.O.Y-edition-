// hal_input.h
#pragma once
#include <stdint.h>
#include "os/events.h"

void    hal_input_init();
void    hal_input_tick();       // call each frame
uint8_t hal_input_pot();        // smoothed 0–255
bool    hal_input_btn(uint8_t idx);  // current state, 0=A 1=B 2=C