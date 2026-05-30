#include "hal_gps.h"
#include <HardwareSerial.h>
#include <TinyGPS++.h>

// NEO-6M GPS: UART2 GPIO 16 RX, 17 TX, 9600 baud
static HardwareSerial _gps_serial(2);
static TinyGPSPlus _gps;
static float _lat = 0.0f;
static float _lon = 0.0f;
static float _speed = 0.0f;
static uint8_t _sats = 0;
static uint8_t _locked = 0;

void hal_gps_init() {
    // Initialize UART2: RX=GPIO 16, TX=GPIO 17, 9600 baud
    _gps_serial.begin(9600, SERIAL_8N1, 16, 17);
}

void hal_gps_update() {
    // Read available data from GPS serial
    while (_gps_serial.available()) {
        _gps.encode(_gps_serial.read());
    }
    
    // Update cached values if new data received
    if (_gps.location.isUpdated()) {
        _lat = _gps.location.lat();
        _lon = _gps.location.lng();
        _locked = (_gps.location.isValid()) ? 1 : 0;
    }
    
    if (_gps.speed.isUpdated()) {
        _speed = _gps.speed.knots();
    }
    
    if (_gps.satellites.isUpdated()) {
        _sats = _gps.satellites.value();
    }
}

float hal_gps_lat() {
    return _lat;
}

float hal_gps_lon() {
    return _lon;
}

float hal_gps_speed() {
    return _speed;
}

uint8_t hal_gps_sats() {
    return _sats;
}

uint8_t hal_gps_locked() {
    return _locked;
}
