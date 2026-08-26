// app_fuzz.h — slot APP_FUZZ: HTTP request fuzzer.
//
// Sub-modes:
//    0. TARGET   — pick a preset target from `fuzz_targets` hal_storage
//                  key (4 presets, comma-separated) using the POT.
//    1. REPS     — pick a request count from a small pool (16 / 64 /
//                  256 / 1024). Persists across runs.
//    2. RUN      — start the fuzzer. Each tick sends the next request,
//                  records status and latency to
//                  /var/lib/void-os/loot/fuzz_<UTC>.csv.

#pragma once
#include "../os/events.h"

void app_fuzz_init();
void app_fuzz_tick();
void app_fuzz_draw();
void app_fuzz_event(Event e);
void app_fuzz_suspend();
