// app_leak.h — leak scanner. SCAN/HASH/BREACH/SETTINGS -> leaks.csv.
// Shells out to grep/hashcat/curl when present, degrades bare-box.

#pragma once
#include "os/events.h"

void app_leak_init();
void app_leak_tick();
void app_leak_draw();
void app_leak_event(Event e);
void app_leak_suspend();