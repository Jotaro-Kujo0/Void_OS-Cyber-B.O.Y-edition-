#ifndef APP_RADIO_H
#define APP_RADIO_H

#include "../os/events.h"

void app_radio_init();
void app_radio_tick();
void app_radio_draw();
void app_radio_event(Event e);
void app_radio_suspend();

#endif // APP_RADIO_H