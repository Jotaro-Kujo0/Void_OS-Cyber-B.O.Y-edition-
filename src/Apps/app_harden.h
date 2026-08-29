// app_harden.h — device self-hardening dashboard.
// UFW/FAIL2BAN/ROOTKIT/LYNIS/UPDATE; auto-detect tools, degrade cleanly.

#pragma once
#include "os/events.h"

void app_harden_init();
void app_harden_tick();
void app_harden_draw();
void app_harden_event(Event e);
void app_harden_suspend();