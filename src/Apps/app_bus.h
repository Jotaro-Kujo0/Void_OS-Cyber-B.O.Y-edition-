//app_bus.h - hardware sniffer (slot APP_BUS)
//
// sub modes:
// logic - 4-channel logic analyzer
// wıegand - access-control D0/D1 sniffer
// ibutton - DS1990A reader/emulator 1 wire
// console- direct cable passthrough UART
// remote - TCp remote console (ssh-style type shi)
// GPIO - read/write any GPIO pin
// DTMF - DTMF / POCSAG / FSK tone decoder

#pragma once
#include "../os/events.h"

void app_bus_init();
void app_bus_tick();
void app_bus_draw();
void app_bus_event(Event e);
void app_bus_suspend();