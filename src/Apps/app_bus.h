//app_bus.h - hardware sniffer (slot APP_BUS)
// logic / wıegand / ibutton / console / remote / GPIO / DTMF

#pragma once
#include "../os/events.h"

void app_bus_init();
void app_bus_tick();
void app_bus_draw();
void app_bus_event(Event e);
void app_bus_suspend();