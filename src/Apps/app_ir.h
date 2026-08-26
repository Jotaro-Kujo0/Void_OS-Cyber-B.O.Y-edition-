// app_ir.h
#pragma once
#include "../os/events.h"

void app_ir_init();
void app_ir_tick();
void app_ir_draw();
void app_ir_event(Event e);
void app_ir_suspend();
void app_ir_resume();
