// hal_radio.h
#pragma once
#include <stdint.h>

void hal_radio_init();
uint8_t hal_radio_rssi();              // Signal strength 0-255
uint8_t hal_radio_scan();              // Detected signal count
void hal_radio_send(float freq_mhz, const uint8_t *data, uint8_t len);
int8_t hal_radio_receive(uint8_t *buf, uint8_t *len);  // Returns -1 if no data
