#include "hal_radio.h"
#include <SPI.h>
#include <ELECHOUSE_CC1101.h>

// CC1101 SPI: GPIO 18/19/23 (standard SPI1), CS=GPIO 15, GDO0=GPIO 26
static ELECHOUSE_CC1101 _cc1101;
static uint8_t _rssi = 0;
static uint8_t _scan_count = 0;

void hal_radio_init() {
    SPI.begin(18, 19, 23, 15);  // SCK, MISO, MOSI, CS
    
    _cc1101.setCS(15);
    _cc1101.setGDO0(26);
    
    // Initialize CC1101
    if (!_cc1101.Init()) {
        // Fallback: radio not available
        return;
    }
    
    // Tune to 433MHz
    _cc1101.setMHZ(433.05);
    _cc1101.setCCKON();
    _rssi = 0;
    _scan_count = 0;
}

uint8_t hal_radio_rssi() {
    // Read RSSI (Received Signal Strength Indicator)
    // CC1101 RSSI is 0-255, map to 0-100 for display
    int8_t rssi_raw = _cc1101.getRxStatus();
    if (rssi_raw < 0) {
        _rssi = 0;
    } else {
        _rssi = (rssi_raw * 100) / 256;
    }
    return _rssi;
}

uint8_t hal_radio_scan() {
    // Scan for signals on current frequency
    // Returns count of detected packets (simple implementation)
    _scan_count = 0;
    
    // Check for incoming data (non-blocking)
    if (_cc1101.CheckRxFifo() > 0) {
        _scan_count = _cc1101.CheckRxFifo();
    }
    
    return _scan_count;
}

void hal_radio_send(float freq_mhz, const uint8_t *data, uint8_t len) {
    if (!data || len == 0) return;
    
    _cc1101.setMHZ(freq_mhz);
    _cc1101.SendData((uint8_t*)data, len);
}

int8_t hal_radio_receive(uint8_t *buf, uint8_t *len) {
    if (!buf || !len) return -1;
    
    int rx_len = _cc1101.CheckRxFifo();
    if (rx_len <= 0) return -1;
    
    if (rx_len > *len) rx_len = *len;
    
    // Read FIFO
    _cc1101.ReadData(buf, (uint8_t*)&rx_len);
    *len = rx_len;
    
    return 0;
}
