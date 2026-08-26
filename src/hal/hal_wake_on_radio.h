// hal_wake_on_radio.h — interrupt-driven CC1101 wake.
//
// The CC1101 wakes the device on a GDO0 pin transition. Idle sleep +
// GDO0 capture = a deep sleep mode that wakes only on incoming RF.

#pragma once
#include <stdint.h>
#include <stdbool.h>

void hal_wor_init(void);         // configure CC1101 WOR mode
void hal_wor_arm(void);          // register GDO0 interrupt
void hal_wor_disarm(void);
