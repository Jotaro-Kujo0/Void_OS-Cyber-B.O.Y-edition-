#include "hal_gps.h"

#ifdef VOIDOS_RPI5
#include <cerrno>
#include <cmath>
#include <fcntl.h>
#include <string>
#include <termios.h>
#include <unistd.h>

static int _gps_fd = -1;
static float _lat = 0.0f, _lon = 0.0f, _speed = 0.0f;
static uint8_t _sats = 0, _locked = 0;
static std::string _line;

static float nmea_coord(const std::string &value, const std::string &hemisphere) {
    if (value.size() < 3) return 0.0f;
    const double raw = std::strtod(value.c_str(), nullptr);
    const double degrees = std::floor(raw / 100.0);
    float result = static_cast<float>(degrees + (raw - degrees * 100.0) / 60.0);
    if (hemisphere == "S" || hemisphere == "W") result = -result;
    return result;
}

static void parse_nmea(const std::string &line) {
    if (line.size() < 7 || line[0] != '$') return;
    std::string fields[16]; size_t start = 1; int count = 0;
    while (start <= line.size() && count < 16) {
        size_t end = line.find(',', start);
        if (end == std::string::npos) end = line.size();
        fields[count++] = line.substr(start, end - start);
        if (end == line.size()) break;
        start = end + 1;
    }
    if (count < 1) return;
    if (fields[0].find("RMC") != std::string::npos && count > 7) {
        _locked = fields[2] == "A";
        if (_locked) {
            _lat = nmea_coord(fields[3], fields[4]);
            _lon = nmea_coord(fields[5], fields[6]);
            _speed = static_cast<float>(std::strtod(fields[7].c_str(), nullptr));
        }
    } else if (fields[0].find("GGA") != std::string::npos && count > 7) {
        _sats = static_cast<uint8_t>(std::strtoul(fields[7].c_str(), nullptr, 10));
        if (fields[6] != "0") _locked = true;
    }
}

void hal_gps_init() {
    _gps_fd = open("/dev/serial0", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (_gps_fd < 0) return;
    termios settings{};
    if (tcgetattr(_gps_fd, &settings) == 0) {
        cfsetispeed(&settings, B9600); cfsetospeed(&settings, B9600);
        settings.c_cflag = (settings.c_cflag & ~CSIZE) | CS8;
        settings.c_cflag |= CREAD | CLOCAL;
        settings.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
        tcsetattr(_gps_fd, TCSANOW, &settings);
    }
}
void hal_gps_update() {
    if (_gps_fd < 0) return;
    char buffer[256]; ssize_t count;
    while ((count = read(_gps_fd, buffer, sizeof(buffer))) > 0)
        for (ssize_t i=0;i<count;++i) { if (buffer[i]=='\n') { parse_nmea(_line); _line.clear(); } else if (buffer[i]!='\r') _line += buffer[i]; }
}
float hal_gps_lat() { return _lat; }
float hal_gps_lon() { return _lon; }
float hal_gps_speed() { return _speed; }
uint8_t hal_gps_sats() { return _sats; }
uint8_t hal_gps_locked() { return _locked; }

#else
#include <HardwareSerial.h>
#include <TinyGPS++.h>
static HardwareSerial _gps_serial(2);
static TinyGPSPlus _gps;
static float _lat=0.0f, _lon=0.0f, _speed=0.0f;
static uint8_t _sats=0, _locked=0;
void hal_gps_init() { _gps_serial.begin(9600, SERIAL_8N1, 16, 17); }
void hal_gps_update() { while (_gps_serial.available()) _gps.encode(_gps_serial.read()); if (_gps.location.isUpdated()) { _lat=_gps.location.lat(); _lon=_gps.location.lng(); _locked=_gps.location.isValid(); } if (_gps.speed.isUpdated()) _speed=_gps.speed.knots(); if (_gps.satellites.isUpdated()) _sats=_gps.satellites.value(); }
float hal_gps_lat() { return _lat; }
float hal_gps_lon() { return _lon; }
float hal_gps_speed() { return _speed; }
uint8_t hal_gps_sats() { return _sats; }
uint8_t hal_gps_locked() { return _locked; }
#endif
