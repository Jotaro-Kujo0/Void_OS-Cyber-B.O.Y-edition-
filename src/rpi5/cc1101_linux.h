#pragma once

#include <cstddef>
#include <cstdint>

// Transport-only CC1101 base driver for Raspberry Pi 5.
//
// This file intentionally stops below the application layer. Before enabling
// transmission, add a region-appropriate register profile and a GDO0 GPIO
// interrupt/polling strategy. The CC1101 is a radio transceiver, so frequency,
// bandwidth, power, and duty-cycle settings must be reviewed for the target
// jurisdiction and hardware antenna.
class Cc1101Linux {
public:
    Cc1101Linux() = default;
    ~Cc1101Linux();

    bool open(const char *device = "/dev/spidev0.1", uint32_t speed_hz = 4000000);
    void close();
    bool is_open() const { return _fd >= 0; }

    bool write_register(uint8_t address, uint8_t value);
    bool read_register(uint8_t address, uint8_t &value);
    bool write_burst(uint8_t address, const uint8_t *data, std::size_t length);
    bool read_burst(uint8_t address, uint8_t *data, std::size_t length);
    bool strobe(uint8_t command);

    // These helpers are deliberately low-level. A complete driver still needs
    // reset/configure, GDO0 state, packet CRC handling, and overflow recovery.
    bool transmit_fifo(const uint8_t *data, std::size_t length);
    bool receive_fifo(uint8_t *data, std::size_t capacity, std::size_t &length);
    bool receive_available();

    // ── High-level radio control ───────────────────────────────────
    // Register profile matches SmartRC-CC1101-Driver-Lib defaults so
    // Pi 5 and ESP32 radios use the same wire format.
    bool configure();                  // SRES + full register profile
    bool set_frequency(float mhz);     // FREQ write + band calibration
    bool set_modulation(uint8_t mod);  // 0=2FSK 1=GFSK 2=OOK 3=4FSK 4=MSK
    bool set_data_rate(uint16_t baud); // SmartRC DATARATE formula
    bool set_rx_bw(float khz);         // SmartRC channel-BW formula
    bool set_deviation(float khz);     // SmartRC DEVIATN formula
    bool set_rx();                     // flush RX FIFO, enter RX
    bool set_idle();                   // enter IDLE
    bool tx_packet(const uint8_t *data, uint8_t len);  // length-prefixed packet
    bool tx_raw_stream(const uint8_t *data, std::size_t len);  // async streaming
    bool rx_available();               // RX FIFO non-empty
    int8_t rx_packet(uint8_t *data, uint8_t capacity);   // -1 if none
    int8_t rssi();                     // raw RSSI byte

private:
    int _fd = -1;
    uint32_t _speed_hz = 4000000;
    float _mhz = 433.92f;
    uint8_t _mod = 2;

    bool transfer(const uint8_t *tx, uint8_t *rx, std::size_t length);
    bool calibrate();                  // band-specific FS tuning
    bool set_pa();                     // PA table for current band/mod
    uint64_t ms_now() const;
};
