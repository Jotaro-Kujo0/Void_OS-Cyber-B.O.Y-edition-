// app_drop.h — USB drop-attack artefact generator (slot APP_DROP)
//
// Sub-modes:
//   0. TARGET  — select OS template (WIN/LIN/MAC/HID/GENERIC)
//   1. PAYLOAD — choose or edit the payload script/binary
//   2. BUILD   — generate artefact files from template + payload
//   3. DROP    — activate USB-MSC gadget to expose loot to target
//
// Requires: configfs USB gadget support in /boot/firmware/config.txt
//           dtoverlay=dwc2

#pragma once
#include "../os/events.h"

void app_drop_init();
void app_drop_tick();
void app_drop_draw();
void app_drop_event(Event e);
void app_drop_suspend();

// Cross-app: check if gadget is active
bool app_drop_gadget_active();
