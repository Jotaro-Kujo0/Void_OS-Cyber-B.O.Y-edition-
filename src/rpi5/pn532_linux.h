#pragma once

#include <cstddef>
#include <cstdint>

struct Pn532Uid {
    uint8_t bytes[10] = {};
    std::size_t length = 0;
};

// Minimal PN532 I2C transport. The app should add authentication, block
// operations, tag removal detection, and persistent-record policy above this
// class. The driver never writes tag memory automatically.
class Pn532I2c {
public:
    ~Pn532I2c();

    bool open(const char *device = "/dev/i2c-1", uint8_t address = 0x24);
    void close();
    bool is_open() const { return _fd >= 0; }
    bool wake();
    bool poll_uid(Pn532Uid &uid, uint32_t timeout_ms = 500);

    // MIFARE Classic authenticated access.
    bool mifare_auth(uint8_t block, const uint8_t key[6]);
    bool mifare_read(uint8_t block, uint8_t out[16]);

    // ISO14443A UID emulation (requires SAMConfig virtual-card mode).
    bool emulate_uid(const uint8_t *uid, uint8_t len);
    void emulate_stop();

private:
    int _fd = -1;

    bool write_frame(const uint8_t *payload, std::size_t length);
    bool read_frame(uint8_t *payload, std::size_t capacity, std::size_t &length,
                    uint32_t timeout_ms);
    bool wait_ready(uint32_t timeout_ms);
};
