// hal_serial_term.h — line-based REPL on the USB-CDC channel.
//
// Operators drive the device from a laptop terminal without touching
// the screen or the buttons. Each newline-terminated command runs and
// returns a short response. The REPL is fire-and-forget: spawn the
// parser in `events` task; output goes back via Serial.print (USB-CDC).
//
// Same protocol is mirrored by `app_bus`'s CONSOLE mode (where the
// USB-CDC line is forwarded into a hostname:port socket). For the
// device-direct case, this HAL consumes the bytes and replies.

#pragma once
#include <stdint.h>
#include <stdbool.h>

void hal_serial_term_init();
void hal_serial_term_tick();   // call from input_task()

// Send a one-liner to the host. Used by async results.
void hal_serial_term_respond(const char *line);

// Optional: the operator can save a script that runs at boot. Path:
//   /loot/term.rc on Pi 5, /sd/term.rc on ESP32.
bool hal_serial_term_run_script(const char *path);
