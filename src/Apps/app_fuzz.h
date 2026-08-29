// app_fuzz.h — HTTP fuzzer. TARGET / REPS / RUN -> loot/fuzz_<UTC>.csv.

#pragma once
#include "../os/events.h"

void app_fuzz_init();
void app_fuzz_tick();
void app_fuzz_draw();
void app_fuzz_event(Event e);
void app_fuzz_suspend();
