// hal_gps.h
#pragma once
#include <stdint.h>

void hal_gps_init();
void hal_gps_update();             // Poll new NMEA data
float hal_gps_lat();               // Latitude (decimal degrees)
float hal_gps_lon();               // Longitude (decimal degrees)
float hal_gps_speed();             // Speed in knots
uint8_t hal_gps_sats();            // Satellite count
uint8_t hal_gps_locked();          // 1 if fix achieved, 0 otherwise
