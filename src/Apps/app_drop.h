// app_drop.h — USB drop-artefact generator. TARGET/PAYLOAD/BUILD/DROP.
// Needs configfs USB gadget, dtoverlay=dwc2.

#pragma once
#include "../os/events.h"

void app_drop_init();
void app_drop_tick();
void app_drop_draw();
void app_drop_event(Event e);
void app_drop_suspend();

// Cross-app: check if gadget is active
bool app_drop_gadget_active();
