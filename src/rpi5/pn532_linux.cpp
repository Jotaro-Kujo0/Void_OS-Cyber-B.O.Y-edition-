#include "pn532_linux.h"

#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

namespace {
constexpr uint8_t HOST_TO_PN532 = 0xD4;
constexpr uint8_t PN532_TO_HOST = 0xD5;
constexpr uint8_t INLIST_PASSIVE_TARGET = 0x4A;
constexpr uint8_t SAMCONFIG = 0x14;
constexpr uint8_t INDATAEXCHANGE = 0x40;
constexpr uint8_t TGINITASTARGET = 0x8C;
constexpr uint8_t MIFARE_READ = 0x30;
constexpr uint8_t MIFARE_AUTH_A = 0x60;
}

Pn532I2c::~Pn532I2c() { close(); }

bool Pn532I2c::open(const char *device, uint8_t address) {
    close();
    _fd = ::open(device, O_RDWR | O_CLOEXEC);
    if (_fd < 0 || ioctl(_fd, I2C_SLAVE, address) < 0) {
        close();
        return false;
    }
    return wake();
}

void Pn532I2c::close() {
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }
}

bool Pn532I2c::write_frame(const uint8_t *payload, std::size_t length) {
    if (_fd < 0 || !payload || length == 0 || length > 254) return false;
    // PN532 normal frame: preamble, start codes, LEN/LCS, payload, DCS, postamble.
    uint8_t frame[261] = {};
    const uint8_t len = static_cast<uint8_t>(length);
    frame[0] = 0x00; frame[1] = 0x00; frame[2] = 0xFF;
    frame[3] = len; frame[4] = static_cast<uint8_t>(~len + 1);
    for (std::size_t i = 0; i < length; ++i) frame[5 + i] = payload[i];
    uint8_t checksum = 0;
    for (std::size_t i = 0; i < length; ++i) checksum = static_cast<uint8_t>(checksum + payload[i]);
    frame[5 + length] = static_cast<uint8_t>(~checksum + 1);
    frame[6 + length] = 0x00;
    return ::write(_fd, frame, length + 7) == static_cast<ssize_t>(length + 7);
}

bool Pn532I2c::wait_ready(uint32_t timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        uint8_t status = 0;
        if (::read(_fd, &status, 1) == 1 && status == 0x01) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool Pn532I2c::read_frame(uint8_t *payload, std::size_t capacity,
                          std::size_t &length, uint32_t timeout_ms) {
    length = 0;
    if (_fd < 0 || !payload || capacity == 0 || !wait_ready(timeout_ms)) return false;
    uint8_t header[5] = {};
    if (::read(_fd, header, sizeof(header)) != static_cast<ssize_t>(sizeof(header))) return false;
    if (header[0] != 0x00 || header[1] != 0x00 || header[2] != 0xFF) return false;
    const uint8_t frame_length = header[3];
    if (static_cast<uint8_t>(header[3] + header[4]) != 0 || frame_length < 2 || frame_length > capacity) return false;
    if (::read(_fd, payload, frame_length) != frame_length) return false;
    uint8_t checksum = 0;
    for (uint8_t i = 0; i < frame_length; ++i) checksum = static_cast<uint8_t>(checksum + payload[i]);
    uint8_t dcs_and_postamble[2] = {};
    if (::read(_fd, dcs_and_postamble, sizeof(dcs_and_postamble)) != static_cast<ssize_t>(sizeof(dcs_and_postamble))) return false;
    if (static_cast<uint8_t>(checksum + dcs_and_postamble[0]) != 0 || dcs_and_postamble[1] != 0x00) return false;
    length = frame_length;
    return true;
}

bool Pn532I2c::wake() {
    if (_fd < 0) return false;
    const uint8_t wake_frame[] = {0x55, 0x55, 0x00, 0x00, 0x00};
    (void)::write(_fd, wake_frame, sizeof(wake_frame));
    const uint8_t command[] = {HOST_TO_PN532, 0x02}; // GetFirmwareVersion
    if (!write_frame(command, sizeof(command))) return false;
    uint8_t response[32] = {}; std::size_t length = 0;
    return read_frame(response, sizeof(response), length, 1000) &&
           length >= 2 && response[0] == PN532_TO_HOST && response[1] == 0x02;
}

bool Pn532I2c::poll_uid(Pn532Uid &uid, uint32_t timeout_ms) {
    uid.length = 0;
    const uint8_t command[] = {HOST_TO_PN532, INLIST_PASSIVE_TARGET, 0x01, 0x00};
    if (!write_frame(command, sizeof(command))) return false;
    uint8_t response[64] = {}; std::size_t length = 0;
    if (!read_frame(response, sizeof(response), length, timeout_ms) || length < 8) return false;
    if (response[0] != PN532_TO_HOST || response[1] != INLIST_PASSIVE_TARGET || response[2] == 0) return false;
    // Response: TFI, command, target count, target number, SENS_RES(2), SEL_RES, UID length, UID.
    const std::size_t uid_length_index = 7;
    const std::size_t uid_index = 8;
    const std::size_t available = length > uid_index ? length - uid_index : 0;
    const std::size_t count = response[uid_length_index] < available ? response[uid_length_index] : available;
    if (count == 0 || count > sizeof(uid.bytes)) return false;
    for (std::size_t i = 0; i < count; ++i) uid.bytes[i] = response[uid_index + i];
    uid.length = count;
    return true;
}

bool Pn532I2c::mifare_auth(uint8_t block, const uint8_t key[6]) {
    if (_fd < 0 || !key) return false;
    // InDataExchange: authenticate block with Key A.
    const uint8_t command[] = {HOST_TO_PN532, INDATAEXCHANGE, 0x01,
                               MIFARE_AUTH_A, block, key[0], key[1], key[2],
                               key[3], key[4], key[5]};
    if (!write_frame(command, sizeof(command))) return false;
    uint8_t response[32] = {}; std::size_t length = 0;
    if (!read_frame(response, sizeof(response), length, 500) || length < 3) return false;
    return response[0] == PN532_TO_HOST &&
           response[1] == INDATAEXCHANGE && response[2] == 0x00;
}

bool Pn532I2c::mifare_read(uint8_t block, uint8_t out[16]) {
    if (_fd < 0 || !out) return false;
    // InDataExchange: read 16-byte block after authentication.
    const uint8_t command[] = {HOST_TO_PN532, INDATAEXCHANGE, 0x01,
                               MIFARE_READ, block};
    if (!write_frame(command, sizeof(command))) return false;
    uint8_t response[32] = {}; std::size_t length = 0;
    if (!read_frame(response, sizeof(response), length, 500) || length < 19) return false;
    if (response[0] != PN532_TO_HOST ||
        response[1] != INDATAEXCHANGE || response[2] != 0x00) return false;
    std::memcpy(out, &response[3], 16);
    return true;
}

bool Pn532I2c::emulate_uid(const uint8_t *uid, uint8_t len) {
    if (_fd < 0 || !uid || (len != 4 && len != 7)) return false;
    // Switch to virtual-card mode so the PN532 acts as a passive tag.
    const uint8_t sam[] = {HOST_TO_PN532, SAMCONFIG, 0x01, 0x14, 0x00};
    if (!write_frame(sam, sizeof(sam))) return false;
    uint8_t response[32] = {}; std::size_t length = 0;
    if (!read_frame(response, sizeof(response), length, 500) || length < 2) return false;
    if (response[0] != PN532_TO_HOST) return false;
    // TgInitAsTarget: auto mode, standard MIFARE params, ATQA/SAK by UID size.
    uint8_t command[64];
    command[0] = HOST_TO_PN532;
    command[1] = TGINITASTARGET;
    command[2] = 0x00;                                // mode: auto
    command[3] = 0x08; command[4] = 0x00; command[5] = 0x12;
    command[6] = 0x34; command[7] = 0x56; command[8] = 0x00;
    command[9]  = (len == 4) ? 0x04 : 0x44;           // ATQA
    command[10] = 0x00;
    command[11] = (len == 4) ? 0x08 : 0x20;           // SAK
    command[12] = len;
    std::memcpy(&command[13], uid, len);
    const std::size_t command_length = 13 + len;
    if (!write_frame(command, command_length)) return false;
    return read_frame(response, sizeof(response), length, 500) &&
           length >= 2 && response[0] == PN532_TO_HOST;
}

void Pn532I2c::emulate_stop() {
    if (_fd < 0) return;
    // Back to normal reader mode.
    const uint8_t sam[] = {HOST_TO_PN532, SAMCONFIG, 0x00, 0x14, 0x00};
    if (!write_frame(sam, sizeof(sam))) return;
    uint8_t response[32] = {}; std::size_t length = 0;
    read_frame(response, sizeof(response), length, 500);
}
