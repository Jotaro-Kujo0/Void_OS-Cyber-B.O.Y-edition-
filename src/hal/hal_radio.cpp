#include "hal_radio.h"
#include <SPI.h>
#include <SmartRC_CC1101.h>

// The library uses this specific class name
ELECHOUSE_CC1101 _cc1101; 

static uint8_t _rssi = 0;
static uint8_t _scan_count = 0;

void hal_radio_init() {
    // SPI pins
    _cc1101.setSpiPin(18, 19, 23, 15);
    _cc1101.setGDO0(26);
    
    // The library method is explicitly Init()
    _cc1101.Init();
    
    _cc1101.setMHZ(433.05);
}

uint8_t hal_radio_rssi() {
    return (uint8_t)_cc1101.getRssi();
}

uint8_t hal_radio_scan() {
    return _cc1101.CheckRxFifo(0);
}

void hal_radio_send(float freq_mhz, const uint8_t *data, uint8_t len) {
    if (!data || len == 0) return;
    
    _cc1101.setMHZ(freq_mhz);
    
    _cc1101.SendData((uint8_t*)data, len);
}

int8_t hal_radio_receive(uint8_t *buf, uint8_t *len) {
    if (!buf || !len) return -1;
    
    // Checking if data is available
    if (_cc1101.CheckRxFifo(0) == 0) return -1;
    
    // ReceiveData returns the number of bytes read
    int rx_len = _cc1101.ReceiveData(buf);
    
    if (rx_len <= 0) return -1;
    
    *len = (uint8_t)rx_len;
    return 0;
}