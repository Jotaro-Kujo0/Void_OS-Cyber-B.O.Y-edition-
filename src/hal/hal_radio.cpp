#include "hal_radio.h"
#include "hal_pocsag.h"

#ifdef VOIDOS_RPI5
#include "../rpi5/cc1101_linux.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>

// ── Radio driver state ────────────────────────────────────────────────
static Cc1101Linux   _cc;
static bool          _ok     = false;
static uint8_t       _rssi   = 0;
static RadioBand     _band   = RADIO_BAND_433;
static RadioModulation _mod  = RADIO_MOD_ASK_OOK;

static const float BAND_MHZ[4]   = {315.0f, 433.92f, 868.3f, 915.0f};
static const float SWEEP_START[4] = {314.0f, 433.0f, 868.0f, 914.0f};
static const float SWEEP_END[4]   = {316.0f, 435.0f, 870.0f, 916.0f};
static const float SWEEP_STEP     = 0.1f;

static uint32_t sys_ms() {
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// HAL modulation enum → driver code (OOK, FSK, GFSK, MSK).
static uint8_t radio_mod_to_driver(RadioModulation m) {
    switch (m) {
        case RADIO_MOD_ASK_OOK:
        case RADIO_MOD_ASK_RAW:  return 2;   // ASK/OOK
        case RADIO_MOD_FSK_1_2:
        case RADIO_MOD_FSK_5_8:  return 0;   // 2-FSK
        case RADIO_MOD_GFSK_9_6:
        case RADIO_MOD_GFSK_38:
        case RADIO_MOD_GFSK_100: return 1;   // GFSK
        case RADIO_MOD_MSK_250:
        case RADIO_MOD_MSK_500:  return 4;   // MSK
    }
    return 2;
}

// ── Capture ring (bounded by RADIO_CAPTURE_BYTES) ─────────────────────
#define CAP_SLOTS 8
struct CapPkt { uint8_t data[61]; uint8_t len; float freq; uint32_t ts; };
static CapPkt _cap[CAP_SLOTS];
static uint8_t _cap_head = 0, _cap_tail = 0, _cap_n = 0;
static bool   _capturing  = false;
static float  _cap_freq   = 433.92f;
static CapPkt _last_tx;
static bool   _has_last_tx = false;

// ── Sweep state ───────────────────────────────────────────────────────
static bool _sweeping = false;
static float _sweep_freq = 0.0f;
static RadioSweepBin _sweep_bins[32];
static uint16_t _sweep_n = 0;

// ── Fixed-code generator state ────────────────────────────────────────
static bool     _fixed_on  = false;
static uint64_t _fixed_code = 0;
static uint64_t _fixed_max  = 0;

// ── Base API ──────────────────────────────────────────────────────────

void hal_radio_init() {
    _ok = _cc.open("/dev/spidev0.1");
    if (_ok) _ok = _cc.configure();
    if (_ok) _ok = _cc.set_frequency(BAND_MHZ[_band]);
}

uint8_t hal_radio_rssi() { return _ok ? static_cast<uint8_t>(_cc.rssi()) : 0; }
uint8_t hal_radio_scan() { return (_ok && _cc.rx_available()) ? 1 : 0; }

void hal_radio_send(float freq_mhz, const uint8_t *data, uint8_t len) {
    if (!_ok || !data || !len) return;
    _cc.set_frequency(freq_mhz);
    _cc.tx_packet(data, len);
}

int8_t hal_radio_receive(uint8_t *buf, uint8_t *len) {
    if (!_ok || !buf || !len) return -1;
    int8_t n = _cc.rx_packet(buf, *len);
    if (n <= 0) return -1;
    *len = static_cast<uint8_t>(n);
    return 0;
}

bool hal_radio_set_band(RadioBand b) {
    if (b > RADIO_BAND_915) return false;
    _band = b;
    _cap_freq = BAND_MHZ[b];
    return _ok && _cc.set_frequency(BAND_MHZ[b]);
}
RadioBand hal_radio_get_band() { return _band; }

bool hal_radio_set_modulation(RadioModulation m) {
    if (m > RADIO_MOD_MSK_500) return false;
    _mod = m;
    return _ok && _cc.set_modulation(radio_mod_to_driver(m));
}

// ── Capture & replay ──────────────────────────────────────────────────

bool hal_radio_capture_start() {
    if (!_ok) return false;
    _capturing = true;
    _cap_head = _cap_tail = _cap_n = 0;
    _cap_freq = BAND_MHZ[_band];
    return _cc.set_rx();
}

bool hal_radio_capture_step(uint8_t *out, uint8_t *out_len,
                            float *freq_mhz, uint32_t *timestamp_ms) {
    if (!out || !out_len || !freq_mhz || !timestamp_ms) return false;
    // Pull one packet off the radio into the ring.
    if (_capturing && _cap_n < CAP_SLOTS && _cc.rx_available()) {
        uint8_t buf[61];
        int8_t n = _cc.rx_packet(buf, sizeof(buf));
        if (n > 0) {
            CapPkt *p = &_cap[_cap_head];
            std::memcpy(p->data, buf, static_cast<size_t>(n));
            p->len = static_cast<uint8_t>(n);
            p->freq = _cap_freq;
            p->ts = sys_ms();
            _last_tx = *p;
            _has_last_tx = true;
            _cap_head = (_cap_head + 1) % CAP_SLOTS;
            ++_cap_n;
        }
    }
    if (_cap_n == 0) return false;
    CapPkt *p = &_cap[_cap_tail];
    std::memcpy(out, p->data, p->len);
    *out_len = p->len;
    *freq_mhz = p->freq;
    *timestamp_ms = p->ts;
    _cap_tail = (_cap_tail + 1) % CAP_SLOTS;
    --_cap_n;
    return true;
}

void hal_radio_capture_stop() {
    _capturing = false;
    if (_ok) _cc.set_idle();
}

bool hal_radio_capture_replay() {
    if (!_ok || !_has_last_tx) return false;
    _cc.set_frequency(_last_tx.freq);
    return _cc.tx_packet(_last_tx.data, _last_tx.len);
}

bool hal_radio_capture_save_pcap() {
    mkdir("/mnt/void-os/captures", 0755);
    char path[96];
    std::snprintf(path, sizeof(path), "/mnt/void-os/captures/radio_%lu.pcap",
                  (unsigned long)time(nullptr));
    FILE *fp = std::fopen(path, "wb");
    if (!fp) return false;
    uint32_t magic = 0xA1B2C3D4, snaplen = 61, linktype = 0;
    uint16_t ver_maj = 2, ver_min = 4;
    uint32_t reserved = 0;
    std::fwrite(&magic, 4, 1, fp);
    std::fwrite(&ver_maj, 2, 1, fp);
    std::fwrite(&ver_min, 2, 1, fp);
    std::fwrite(&reserved, 4, 1, fp);
    std::fwrite(&snaplen, 4, 1, fp);
    std::fwrite(&linktype, 4, 1, fp);
    for (uint8_t i = 0; i < _cap_n; ++i) {
        CapPkt *p = &_cap[(_cap_tail + i) % CAP_SLOTS];
        uint32_t sec = p->ts / 1000, usec = (p->ts % 1000) * 1000;
        std::fwrite(&sec, 4, 1, fp);
        std::fwrite(&usec, 4, 1, fp);
        std::fwrite(&p->len, 4, 1, fp);
        std::fwrite(&p->len, 4, 1, fp);
        std::fwrite(p->data, p->len, 1, fp);
    }
    std::fclose(fp);
    return true;
}

// ── Spectrum sweep ────────────────────────────────────────────────────

bool hal_radio_sweep_start() {
    if (!_ok) return false;
    _sweeping = true;
    _sweep_freq = SWEEP_START[_band];
    _sweep_n = 0;
    return true;
}

bool hal_radio_sweep_step(RadioSweepBin *out) {
    if (!_sweeping || !out) return false;
    if (_sweep_freq > SWEEP_END[_band] + 0.001f) {
        _sweeping = false;
        _cc.set_idle();
        return false;
    }
    _cc.set_frequency(_sweep_freq);
    _cc.set_rx();
    usleep(30000);                                 // settle
    out->freq_mhz = _sweep_freq;
    out->rssi_dbm = _cc.rssi();
    out->activity = 0;                             // no preamble detect
    if (_sweep_n < sizeof(_sweep_bins) / sizeof(_sweep_bins[0]))
        _sweep_bins[_sweep_n++] = *out;
    _sweep_freq += SWEEP_STEP;
    return true;
}

void hal_radio_sweep_stop() {
    _sweeping = false;
    if (_ok) _cc.set_idle();
}

bool hal_radio_spectrum_save_csv() {
    if (_sweep_n == 0) return false;
    mkdir("/mnt/void-os/rf", 0755);
    char path[96];
    std::snprintf(path, sizeof(path), "/mnt/void-os/rf/spectrum_%lu.csv",
                  (unsigned long)time(nullptr));
    FILE *fp = std::fopen(path, "w");
    if (!fp) return false;
    std::fprintf(fp, "utc,freq_mhz,rssi_dbm,activity\n");
    for (uint16_t i = 0; i < _sweep_n; ++i) {
        std::fprintf(fp, "%lu,%.2f,%d,%u\n",
                     (unsigned long)time(nullptr), _sweep_bins[i].freq_mhz,
                     _sweep_bins[i].rssi_dbm, _sweep_bins[i].activity);
    }
    std::fclose(fp);
    return true;
}

// ── Fixed-code generator ──────────────────────────────────────────────

bool hal_radio_fixed_start(uint8_t width_bits, uint32_t dwell_ms) {
    (void)dwell_ms;
    if (width_bits != 12 && width_bits != 24) return false;
    _fixed_code = 0;
    _fixed_max  = (width_bits == 12) ? 531441ULL : 16777216ULL;  // 3^12 / 2^24
    _fixed_on   = true;
    return true;
}

bool hal_radio_fixed_step(uint64_t *code_out) {
    if (!_fixed_on || !code_out) return false;
    *code_out = _fixed_code;
    _fixed_code = (_fixed_code + 1) % _fixed_max;
    return true;
}

void hal_radio_fixed_stop() { _fixed_on = false; }

// ── POCSAG decoder ─────────────────────────────────────────────────────
// Radio runs 2-FSK at 1200 baud, +-4.5 kHz deviation, ~58 kHz RX BW.
static bool _pocsag_on = false;

bool hal_radio_pocsag_start() {
    if (!_ok) return false;
    if (!_cc.set_modulation(0)) return false;        // 2-FSK
    if (!_cc.set_rx_bw(50.0f)) return false;
    if (!_cc.set_data_rate(1200)) return false;
    if (!_cc.set_deviation(4.5f)) return false;
    if (!_cc.set_frequency(BAND_MHZ[_band])) return false;
    hal_pocsag_reset();
    _pocsag_on = true;
    return _cc.set_rx();
}

bool hal_radio_pocsag_pop(RadioPocsagMsg *out) {
    if (!_pocsag_on || !out) return false;
    const uint32_t now = sys_ms();
    // Drain the async FIFO into the bit decoder.
    uint8_t buf[64];
    std::size_t n = 0;
    while (_cc.receive_fifo(buf, sizeof(buf), n)) {
        for (std::size_t i = 0; i < n; ++i) {
            for (int b = 7; b >= 0; --b)         // MSB first
                hal_pocsag_feed_bit((buf[i] >> b) & 1u, now);
        }
    }
    return hal_pocsag_pop(out);
}

void hal_radio_pocsag_stop() {
    _pocsag_on = false;
    if (!_ok) return;
    _cc.configure();                              // restore default profile
    _cc.set_frequency(BAND_MHZ[_band]);
    _cc.set_modulation(radio_mod_to_driver(_mod));
    _cc.set_idle();
}

// ── Raw OOK bit transmission ──────────────────────────────────────────
bool hal_radio_send_raw_bits(const uint16_t *pulses_us, uint16_t count) {
    if (!_ok || !pulses_us || count == 0) return false;
    // 100 kbaud => 10 us per bit, enough precision for common remotes.
    static uint8_t stream[4096];
    std::size_t total = 0;
    for (uint16_t i = 0; i < count; ++i) {
        uint32_t bits = (static_cast<uint32_t>(pulses_us[i]) + 5) / 10;
        uint8_t level = (i % 2 == 0) ? 0xFF : 0x00;
        if (bits == 0) bits = 1;
        while (bits-- && total < sizeof(stream)) stream[total++] = level;
    }
    if (total == 0) return false;
    if (!_cc.set_modulation(RADIO_MOD_ASK_OOK)) return false;
    // setDRate(100) — SmartRC formula.
    const float baud = 100000.0f;
    uint8_t drate_e = static_cast<uint8_t>(
        std::log(baud * 268435456.0f / (26000000.0f * 256.0f)) / std::log(2.0f));
    uint8_t drate_m = static_cast<uint8_t>(
        baud * 268435456.0f / (26000000.0f * std::pow(2.0f, drate_e)) - 256.0f + 0.5f);
    _cc.set_frequency(BAND_MHZ[_band]);
    bool sent = _cc.tx_raw_stream(stream, total);
    _cc.set_modulation(radio_mod_to_driver(_mod));    // restore profile
    _cc.set_frequency(BAND_MHZ[_band]);
    return sent;
}

#else
#include <SPI.h>
#include <SmartRC_CC1101.h>
ELECHOUSE_CC1101 _cc1101;
static uint8_t _rssi = 0;
static RadioBand       _band = RADIO_BAND_433;
static RadioModulation _mod  = RADIO_MOD_ASK_OOK;

void   hal_radio_init() { _cc1101.setSpiPin(18, 19, 23, 15); _cc1101.setGDO0(26); _cc1101.Init(); _cc1101.setMHZ(433.05); }
uint8_t hal_radio_rssi()         { return static_cast<uint8_t>(_cc1101.getRssi()); }
uint8_t hal_radio_scan()         { return _cc1101.CheckRxFifo(0); }
void   hal_radio_send(float freq_mhz, const uint8_t *data, uint8_t len) {
    if (!data || !len) return;
    _cc1101.setMHZ(freq_mhz);
    _cc1101.SendData((uint8_t*)data, len);
}
int8_t hal_radio_receive(uint8_t *buf, uint8_t *len) {
    if (!buf || !len || _cc1101.CheckRxFifo(0) == 0) return -1;
    int rx_len = _cc1101.ReceiveData(buf);
    if (rx_len <= 0) return -1;
    *len = static_cast<uint8_t>(rx_len);
    return 0;
}

bool   hal_radio_set_band(RadioBand b) {
    const float mhz[] = {315.0f, 433.92f, 868.3f, 915.0f};
    _cc1101.setMHZ(mhz[b]);
    _band = b;
    return true;
}
RadioBand hal_radio_get_band()              { return _band; }
bool   hal_radio_set_modulation(RadioModulation m) { _mod = m; return true; }

bool   hal_radio_capture_start()      { return false; }
bool   hal_radio_capture_step(uint8_t *, uint8_t *, float *, uint32_t *) { return false; }
void   hal_radio_capture_stop()       {}
bool   hal_radio_capture_replay()     { return false; }
bool   hal_radio_capture_save_pcap()  { return false; }

bool   hal_radio_sweep_start()        { return false; }
bool   hal_radio_sweep_step(RadioSweepBin *) { return false; }
void   hal_radio_sweep_stop()         {}
bool   hal_radio_spectrum_save_csv()  { return false; }

bool   hal_radio_fixed_start(uint8_t, uint32_t) { return false; }
bool   hal_radio_fixed_step(uint64_t *) { return false; }
void   hal_radio_fixed_stop()         {}

bool   hal_radio_pocsag_start()       { return false; }
bool   hal_radio_pocsag_pop(RadioPocsagMsg *) { return false; }
void   hal_radio_pocsag_stop()        {}
bool   hal_radio_send_raw_bits(const uint16_t *, uint16_t) { return false; }
#endif
