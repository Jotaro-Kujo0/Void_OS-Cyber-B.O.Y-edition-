// hal_usb_pd.h — USB Power Delivery role swap.
//
// On the laptop USB-C side the device normally enumerates as a UFP
// (sink, draws power). For "power-test" mode the operator wants the
// device to enumerate as a DFP (source, supplies 5V/3A to a target).
// The handoff uses USB-PD messages over CC lines.
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum { PD_ROLE_SINK, PD_ROLE_SOURCE } PdRole;

void hal_usb_pd_init(void);
void hal_usb_pd_set_role(PdRole r);
PdRole hal_usb_pd_get_role(void);
