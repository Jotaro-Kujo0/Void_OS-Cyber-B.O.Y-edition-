// hal_web_ctrl.h — small HTTP control surface.
//
// Operators in the field connect to the device's soft AP (app_rogue)
// and navigate to `http://192.168.4.1/`. The control panel is a single
// HTML page that lists each major app + its current state, plus a row
// of fire-and-forget buttons.
//
// The HTTP server is its own scheduled pump task; running it alongside
// the captive-portal server (app_rogue's CAPTIVE) on a different path
// is fine — both use ports already opened.

#pragma once
#include <stdint.h>
#include <stdbool.h>

void hal_web_ctrl_init();
void hal_web_ctrl_tick();

// Optional HTTP PUT: invert a particular toggle. Used by `/get/csrf`
// after the operator's browser POST. Returns true if accepted.
bool hal_web_ctrl_post(int code, const char *body);
