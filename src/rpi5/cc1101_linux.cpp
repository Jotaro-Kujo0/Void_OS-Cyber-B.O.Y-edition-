#include "cc1101_linux.h"

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
constexpr uint8_t WRITE_BURST = 0x40;
constexpr uint8_t READ_SINGLE = 0x80;
constexpr uint8_t READ_BURST = 0xC0;
constexpr uint8_t FIFO = 0x3F;
constexpr uint8_t RXBYTES = 0x3B;
constexpr uint8_t SFTX = 0x3B;
constexpr uint8_t MARCSTATE = 0x35;
constexpr uint8_t SIDLE = 0x36;
constexpr uint8_t SRX = 0x34;
constexpr uint8_t STX = 0x35;
constexpr uint8_t SFRX = 0x3A;
constexpr uint8_t SRES = 0x30;
constexpr float F_OSC_MHZ = 26.0f;   // 26 MHz crystal
constexpr uint8_t TX_END = 0x15;

// Modulation format bits (SmartRC mapping), 2FSK GFSK OOK 4FSK MSK.
constexpr uint8_t MOD_FORMAT[5] = {0x00, 0x10, 0x30, 0x40, 0x70};

// PA tables per band (SmartRC defaults, highest entry ~12 dBm).
const uint8_t PA_315[8]  = {0x12,0x0D,0x1C,0x34,0x51,0x85,0xCB,0xC2};
const uint8_t PA_433[8]  = {0x12,0x0E,0x1D,0x34,0x60,0x84,0xC8,0xC0};
const uint8_t PA_868[10] = {0x03,0x17,0x1D,0x26,0x37,0x50,0x86,0xCD,0xC5,0xC0};
const uint8_t PA_915[10] = {0x03,0x0E,0x1E,0x27,0x38,0x8E,0x84,0xCC,0xC3,0xC0};

uint8_t lerp(uint8_t a, uint8_t b, float t) {
    return static_cast<uint8_t>(a + (b - a) * t + 0.5f);
}
}

Cc1101Linux::~Cc1101Linux() { close(); }

uint64_t Cc1101Linux::ms_now() const {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

bool Cc1101Linux::open(const char *device, uint32_t speed_hz) {
    close();
    _fd = ::open(device, O_RDWR | O_CLOEXEC);
    if (_fd < 0) return false;
    _speed_hz = speed_hz;
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    if (ioctl(_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(_fd, SPI_IOC_WR_MAX_SPEED_HZ, &_speed_hz) < 0) {
        close();
        return false;
    }
    return true;
}

void Cc1101Linux::close() {
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }
}

bool Cc1101Linux::transfer(const uint8_t *tx, uint8_t *rx, std::size_t length) {
    if (_fd < 0 || !tx || length == 0) return false;
    spi_ioc_transfer message{};
    message.tx_buf = reinterpret_cast<uintptr_t>(tx);
    message.rx_buf = reinterpret_cast<uintptr_t>(rx);
    message.len = static_cast<uint32_t>(length);
    message.speed_hz = _speed_hz;
    message.bits_per_word = 8;
    return ioctl(_fd, SPI_IOC_MESSAGE(1), &message) == static_cast<int>(length);
}

bool Cc1101Linux::write_register(uint8_t address, uint8_t value) {
    const uint8_t tx[2] = {static_cast<uint8_t>(address & 0x3F), value};
    return transfer(tx, nullptr, sizeof(tx));
}

bool Cc1101Linux::read_register(uint8_t address, uint8_t &value) {
    // Status registers (0x30+) are single-byte reads only.
    const uint8_t tx[2] = {static_cast<uint8_t>((address & 0x3F) | READ_SINGLE), 0};
    uint8_t rx[2] = {};
    if (!transfer(tx, rx, sizeof(tx))) return false;
    value = rx[1];
    return true;
}

bool Cc1101Linux::write_burst(uint8_t address, const uint8_t *data, std::size_t length) {
    if (!data || length == 0 || length > 64) return false;
    uint8_t tx[65] = {};
    tx[0] = static_cast<uint8_t>((address & 0x3F) | WRITE_BURST);
    for (std::size_t i = 0; i < length; ++i) tx[i + 1] = data[i];
    return transfer(tx, nullptr, length + 1);
}

bool Cc1101Linux::read_burst(uint8_t address, uint8_t *data, std::size_t length) {
    if (!data || length == 0 || length > 64) return false;
    uint8_t tx[65] = {}, rx[65] = {};
    tx[0] = static_cast<uint8_t>((address & 0x3F) | READ_BURST);
    if (!transfer(tx, rx, length + 1)) return false;
    for (std::size_t i = 0; i < length; ++i) data[i] = rx[i + 1];
    return true;
}

bool Cc1101Linux::strobe(uint8_t command) {
    uint8_t rx = 0;
    return transfer(&command, &rx, 1);
}

bool Cc1101Linux::transmit_fifo(const uint8_t *data, std::size_t length) {
    // Variable-length CC1101 packets are limited to 61 payload bytes here;
    // the packet-length register/profile must be configured by the caller.
    if (!data || length == 0 || length > 61) return false;
    if (!strobe(SFTX) || !write_burst(FIFO, data, length)) return false;
    // The caller must set the radio to TX and monitor GDO0 before treating this
    // as a completed transmission. This is only the FIFO-loading primitive.
    return true;
}

bool Cc1101Linux::receive_available() {
    uint8_t bytes = 0;
    return read_register(RXBYTES, bytes) && (bytes & 0x7F) != 0;
}

bool Cc1101Linux::receive_fifo(uint8_t *data, std::size_t capacity, std::size_t &length) {
    length = 0;
    if (!data || capacity == 0 || !receive_available()) return false;
    uint8_t available = 0;
    if (!read_register(RXBYTES, available)) return false;
    const std::size_t count = (available & 0x7F) < capacity ? (available & 0x7F) : capacity;
    if (!read_burst(FIFO, data, count)) return false;
    length = count;
    return true;
}

// ── High-level radio control ────────────────────────────────────────────

bool Cc1101Linux::configure() {
    if (_fd < 0 || !strobe(SRES)) return false;
    // SmartRC RegConfigSettings profile (async serial, infinite length).
    struct Reg { uint8_t addr, val; };
    static const Reg REGS[] = {
        {0x0B, 0x06}, {0x02, 0x0D}, {0x00, 0x0D}, {0x08, 0x32},
        {0x10, 0x07}, {0x11, 0x93}, {0x13, 0x02}, {0x14, 0xF8},
        {0x0A, 0x00}, {0x15, 0x47}, {0x21, 0x56}, {0x18, 0x18},
        {0x19, 0x16}, {0x1A, 0x1C}, {0x1B, 0xC7}, {0x1C, 0x00},
        {0x1D, 0xB2}, {0x23, 0xE9}, {0x24, 0x2A}, {0x25, 0x00},
        {0x26, 0x1F}, {0x29, 0x59}, {0x2C, 0x81}, {0x2D, 0x35},
        {0x2E, 0x09}, {0x07, 0x04}, {0x09, 0x00}, {0x06, 0x00},
        {0x03, 0x07},
    };
    for (const Reg &r : REGS) {
        if (!write_register(r.addr, r.val)) return false;
    }
    _mhz = 433.92f;
    _mod = 2;
    if (!set_modulation(_mod)) return false;
    return set_frequency(_mhz);
}

bool Cc1101Linux::set_frequency(float mhz) {
    if (_fd < 0) return false;
    if (mhz < 300.0f) mhz = 300.0f;
    if (mhz > 928.0f) mhz = 928.0f;
    const uint32_t freq = static_cast<uint32_t>(mhz * 65536.0f / F_OSC_MHZ);
    if (!write_register(0x0D, (freq >> 16) & 0xFF)) return false;
    if (!write_register(0x0E, (freq >> 8) & 0xFF)) return false;
    if (!write_register(0x0F, freq & 0xFF)) return false;
    _mhz = mhz;
    return calibrate();
}

bool Cc1101Linux::set_modulation(uint8_t mod) {
    if (mod > 4) mod = 4;
    if (_fd < 0) return false;
    uint8_t cfg = 0;
    if (!read_register(0x12, cfg)) return false;         // MDMCFG2
    cfg = static_cast<uint8_t>((cfg & ~0x70) | MOD_FORMAT[mod]);
    if (!write_register(0x12, cfg)) return false;
    if (!write_register(0x22, mod == 2 ? 0x11 : 0x10)) return false;  // FREND0
    _mod = mod;
    return set_pa();
}

bool Cc1101Linux::calibrate() {
    // Band-specific FS tuning (SmartRC Calibrate, clb tables).
    uint8_t fsctrl0 = 0, test0 = 0x09;
    if (_mhz >= 300.0f && _mhz <= 348.0f) {
        fsctrl0 = lerp(24, 28, (_mhz - 300.0f) / 48.0f);
        if (_mhz < 322.88f) test0 = 0x0B;
    } else if (_mhz >= 378.0f && _mhz <= 464.0f) {
        fsctrl0 = lerp(31, 38, (_mhz - 378.0f) / 86.0f);
        if (_mhz < 430.5f) test0 = 0x0B;
    } else if (_mhz >= 779.0f && _mhz <= 899.0f) {
        fsctrl0 = lerp(65, 76, (_mhz - 779.0f) / 120.0f);
        if (_mhz < 861.0f) test0 = 0x0B;
    } else if (_mhz >= 900.0f && _mhz <= 928.0f) {
        fsctrl0 = lerp(77, 79, (_mhz - 900.0f) / 28.0f);
    } else {
        return false;
    }
    if (!write_register(0x0C, fsctrl0)) return false;    // FSCTRL0
    if (!write_register(0x2E, test0)) return false;      // TEST0
    if (test0 == 0x09) {
        uint8_t s = 0;
        if (!read_register(0x24, s)) return false;       // FSCAL2
        if (s < 32) {
            if (!write_register(0x24, s + 32)) return false;
        }
    }
    return set_pa();
}

bool Cc1101Linux::set_pa() {
    const uint8_t *table = nullptr;
    uint8_t idx = 0;
    if (_mhz >= 300.0f && _mhz <= 348.0f)      { table = PA_315; idx = 7; }
    else if (_mhz >= 378.0f && _mhz <= 464.0f) { table = PA_433; idx = 7; }
    else if (_mhz >= 779.0f && _mhz <= 899.0f) { table = PA_868; idx = 9; }
    else if (_mhz >= 900.0f && _mhz <= 928.0f) { table = PA_915; idx = 9; }
    else return false;
    uint8_t pa[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const uint8_t level = table[idx];
    if (_mod == 2) { pa[0] = 0; pa[1] = level; }        // OOK carrier in PA1
    else           { pa[0] = level; pa[1] = 0; }
    return write_burst(0x3E, pa, sizeof(pa));            // PATABLE
}

bool Cc1101Linux::set_data_rate(uint16_t baud) {
    if (_fd < 0) return false;
    float rate = baud < 600 ? 600.0f : (baud > 600000 ? 600000.0f : baud);
    const float drate_e = std::log(rate * 268435456.0f / (26000000.0f * 256.0f)) / std::log(2.0f);
    const uint8_t e = static_cast<uint8_t>(drate_e);
    const uint8_t m = static_cast<uint8_t>(
        rate * 268435456.0f / (26000000.0f * std::pow(2.0f, e)) - 256.0f + 0.5f);
    uint8_t cfg4 = 0;
    if (!read_register(0x10, cfg4)) return false;      // MDMCFG4
    if (!write_register(0x10, (cfg4 & 0xF0) | (e & 0x0F))) return false;
    return write_register(0x11, m);                    // MDMCFG3
}

bool Cc1101Linux::set_rx_bw(float khz) {
    if (_fd < 0) return false;
    const float ratio = 26000000.0f / (8.0f * khz * 1000.0f);
    int e = static_cast<int>(std::log(ratio / 4.0f) / std::log(2.0f));
    if (e < 0) e = 0;
    if (e > 3) e = 3;
    int m = static_cast<int>(std::roundf(ratio / std::pow(2.0f, e) - 4.0f));
    if (m < 0) m = 0;
    if (m > 3) m = 3;
    uint8_t cfg4 = 0;
    if (!read_register(0x10, cfg4)) return false;      // MDMCFG4
    return write_register(0x10, static_cast<uint8_t>((e << 6) | (m << 4) | (cfg4 & 0x0F)));
}

bool Cc1101Linux::set_deviation(float khz) {
    if (_fd < 0) return false;
    const float target = khz * 1000.0f;
    int e = static_cast<int>(std::log(target * 131072.0f / (26000000.0f * 8.0f)) / std::log(2.0f));
    if (e < 0) e = 0;
    if (e > 7) e = 7;
    int m = static_cast<int>(target * 131072.0f / (26000000.0f * std::pow(2.0f, e)) - 8.0f + 0.5f);
    if (m < 0) m = 0;
    if (m > 7) m = 7;
    return write_register(0x15, static_cast<uint8_t>((e << 4) | m));   // DEVIATN
}

bool Cc1101Linux::set_rx() {
    if (_fd < 0) return false;
    return strobe(SFRX) && strobe(SRX);
}

bool Cc1101Linux::set_idle() {
    if (_fd < 0) return false;
    return strobe(SIDLE);
}

bool Cc1101Linux::tx_packet(const uint8_t *data, uint8_t len) {
    if (_fd < 0 || !data || len == 0 || len > 61) return false;
    if (!strobe(SFTX)) return false;
    if (!write_register(FIFO, len)) return false;        // length prefix
    if (!write_burst(FIFO, data, len)) return false;
    if (!strobe(SIDLE) || !strobe(STX)) return false;
    const uint64_t start = ms_now();
    while (ms_now() - start < 500) {                     // wait TX_END
        uint8_t state = 0;
        if (read_register(MARCSTATE, state) && state == TX_END) break;
    }
    strobe(SIDLE);
    return strobe(SFTX);
}

bool Cc1101Linux::tx_raw_stream(const uint8_t *data, std::size_t len) {
    if (_fd < 0 || !data || len == 0) return false;
    if (!strobe(SFTX)) return false;
    std::size_t pos = 0;
    const std::size_t first = len < 61 ? len : 61;
    if (!write_burst(FIFO, data, first)) return false;
    pos = first;
    if (!strobe(SIDLE) || !strobe(STX)) return false;
    while (pos < len) {                                  // stream remaining
        uint8_t tx_bytes = 0;
        if (!read_register(0x3A, tx_bytes)) break;       // TXBYTES
        const uint8_t in_fifo = tx_bytes & 0x7F;
        if (in_fifo >= 63) continue;                     // FIFO full, wait
        const std::size_t room = 63 - in_fifo;
        const std::size_t chunk = (len - pos) < room ? (len - pos) : room;
        if (!write_burst(FIFO, data + pos, chunk)) break;
        pos += chunk;
    }
    const uint64_t start = ms_now();
    while (ms_now() - start < 500) {
        uint8_t state = 0;
        if (read_register(MARCSTATE, state) && state == TX_END) break;
    }
    strobe(SIDLE);
    return strobe(SFTX);
}

bool Cc1101Linux::rx_available() {
    uint8_t bytes = 0;
    return read_register(RXBYTES, bytes) && (bytes & 0x7F) != 0;
}

int8_t Cc1101Linux::rx_packet(uint8_t *data, uint8_t capacity) {
    if (_fd < 0 || !data || capacity == 0) return -1;
    uint8_t nbytes = 0;
    uint64_t start = ms_now();
    bool rearmed = false;
    while (ms_now() - start < 400) {                     // wait length byte
        if (!read_register(RXBYTES, nbytes)) return -1;
        if (nbytes & 0x7F) break;
        if (!rearmed && ms_now() - start > 100) {        // idle recovery
            rearmed = true;
            set_rx();
        }
    }
    if (!(nbytes & 0x7F)) return -1;
    uint8_t len = 0;
    if (!read_register(FIFO, len)) { set_rx(); return -1; }
    if (len == 0 || len > capacity) { set_rx(); return -1; }
    while (ms_now() - start < 600) {                     // wait full payload
        if (read_register(RXBYTES, nbytes) && (nbytes & 0x7F) >= len) break;
    }
    if ((nbytes & 0x7F) < len) { set_rx(); return -1; }
    if (!read_burst(FIFO, data, len)) { set_rx(); return -1; }
    uint8_t status[2] = {};
    read_burst(FIFO, status, sizeof(status));            // appended RSSI/LQI
    set_rx();
    return static_cast<int8_t>(len);
}

int8_t Cc1101Linux::rssi() {
    uint8_t value = 0;
    if (!read_register(0x34, value)) return 0;           // RSSI status
    return static_cast<int8_t>(value);
}
