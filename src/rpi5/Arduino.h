#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <thread>
#include <type_traits>

#ifndef ARDUINO
#define ARDUINO 10819
#endif

#ifndef PI
#define PI 3.14159265358979323846
#endif

using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::int8_t;
using std::int16_t;
using std::int32_t;
using std::int64_t;

enum : uint8_t { INPUT = 0, OUTPUT = 1, INPUT_PULLUP = 2 };
enum : uint8_t { LOW = 0, HIGH = 1 };
enum : uint8_t { SERIAL_8N1 = 0 };

inline uint32_t millis() {
    static const auto start = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count());
}

inline uint32_t micros() {
    static const auto start = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count());
}

inline void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline void pinMode(int, int) {}
inline int digitalRead(int) { return HIGH; }
inline void digitalWrite(int, int) {}
inline int analogRead(int) { return 2048; }

inline long map(long value, long from_low, long from_high,
                long to_low, long to_high) {
    if (from_high == from_low) return to_low;
    return (value - from_low) * (to_high - to_low) /
           (from_high - from_low) + to_low;
}

template <typename T>
inline T constrain(T value, T low, T high) {
    return value < low ? low : (value > high ? high : value);
}

#ifndef pgm_read_word
#define pgm_read_word(address) (*(const uint16_t *)(address))
#endif

class SerialCompat {
public:
    void begin(unsigned long) {}
    template <typename T> void print(const T &value) { std::ostringstream out; out << value; std::fputs(out.str().c_str(), stdout); }
    void print(const char *value) { std::fputs(value ? value : "", stdout); }
    template <typename T> void println(const T &value) { print(value); std::fputc('\n', stdout); }
    void println() { std::fputc('\n', stdout); }
};

inline SerialCompat Serial;

class EspCompat {
public:
    uint32_t getFreeHeap() const { return 0; }
    uint32_t getFreePsram() const { return 0; }
    uint32_t getFlashChipSize() const { return 0; }
};

inline EspCompat ESP;

inline void *ps_malloc(std::size_t size) { return std::malloc(size); }

#ifndef F
#define F(value) value
#endif
