# Void-OS - Cyber B.O.Y. Edition

Void-OS is a custom wearable cyber-deck interface written in C++. It supports the original ESP32 firmware target and a native Raspberry Pi 5/Linux target with the same UI, scheduler, and application architecture.

## Table of Contents
1. [Project Overview](#project-overview)
2. [Software Architecture](#software-architecture)
3. [Application Lifecycle](#application-lifecycle)
4. [Hardware Specifications (BOM)](#hardware-specifications)
5. [Build and Installation](#build-and-installation)
6. [Current Development Status](#current-development-status)

---

## 1. Project Overview

The goal of Void-OS is to provide direct control over a portable hardware stack without a heavy GUI framework. On Raspberry Pi 5, the native target uses Linux SPI (`/dev/spidev0.0`), I2C-1, the GPIO character-device API, POSIX serial GPS parsing, and file-backed settings.

---

## 2. Software Architecture

Void-OS is built on a strict separation of concerns, divided into three main layers:

### The Hardware Abstraction Layer (HAL)
To ensure the OS can be ported to different board layouts in the future, all physical components are managed through dedicated HAL modules:
* `hal_display`: Manages SPI communication and pixel-pushing to the TFT screen.
* `hal_input`: Processes physical inputs (buttons, rotary encoders, potentiometers).
* `hal_power` & `hal_battery`: Monitors power draw, battery levels, and screen dimming states.
* `hal_gps` & `hal_radio`: Interfaces with external communication and location modules.

### The UI Framework
A custom, lightweight graphics engine handles all screen drawing. It bypasses standard heavy GUI libraries in favor of optimized, direct-to-buffer rendering functions (`draw_rect`, `draw_text`, `draw_sprite`) to ensure high framerates on the 2.8-inch display.

### The Modular App System
Applications are siloed from the core OS logic. Current system modules include:
* `app_home`: Core launcher and status dashboard.
* `app_sys`: System settings and memory monitoring.
* `app_radio`: Radio frequency scanning and control.
* `app_nfc`: Near Field Communication read/write interface.
* `app_ir`: Infrared transmission and receiving.
* `app_stat`: Real-time hardware telemetry.

---

## 3. Application Lifecycle

Every application in Void-OS follows a predictable cycle managed by the core OS scheduler. This prevents memory leaks and ensures smooth multitasking.

* `init()`: Called once when the OS boots to allocate necessary memory and establish HAL connections.
* `tick()`: The background logic loop. Executes regardless of whether the app is on screen.
* `draw()`: The rendering loop. Only executes when the application is active and pushed to the display buffer.
* `event()`: The interrupt handler for user inputs (button presses, encoder turns).
* `suspend()` / `resume()`: Handles memory cleanup and state saving when the user navigates away from the app.

---

## 4. Hardware Specifications

The software is specifically engineered for the following hardware stack. 

| Component | Specification / Details | Status |
| :--- | :--- | :--- |
| **Compute** | ESP32 DevKit or Raspberry Pi 5 | Supported |
| **Display** | 2.8-inch ILI9341 SPI TFT, 240x320 | Supported |
| **Input** | PCF8574 buttons over I2C; optional external IIO ADC for pot | Supported on Pi 5 |
| **Storage** | ESP32 NVS or Raspberry Pi XDG state files | Supported |
| **Power Supply** | 3.7V Li-ion battery & charge controller | Hardware-dependent |

---

## 5. Build and Installation

Void-OS is managed via PlatformIO. 

### Prerequisites
* VS Code with the PlatformIO extension installed.
* C++ build tools.
* Python 3.10+ and a virtual environment.

### Compilation
1. Clone the repository.
2. Create the local build environment:
   `python3 -m venv .venv && .venv/bin/pip install -r requirements.txt`
3. Install the declared PlatformIO platforms, frameworks, and libraries:
   `.venv/bin/pio pkg install -e esp32dev`
   `.venv/bin/pio pkg install -e raspberrypi5`
4. Build the ESP32 firmware:
   `.venv/bin/pio run -e esp32dev`
5. Build the Raspberry Pi 5 native executable:
   `.venv/bin/pio run -e raspberrypi5`
   or compile directly with `g++ -std=c++17 -DVOIDOS_RPI5=1 -Isrc/rpi5 -Isrc -Isrc/UI -Isrc/Apps -Isrc/os -Isrc/hal $(find src -name '*.cpp') -o void-os`.

### Raspberry Pi 5 wiring

Use BCM numbering. Enable SPI and I2C with `raspi-config`, then connect the ILI9341 to SPI0 CE0 (`/dev/spidev0.0`), DC to GPIO25, RESET to GPIO24, and connect the PCF8574/MAX17043 to I2C-1 (GPIO2 SDA, GPIO3 SCL). The PCF8574 remains at address `0x20`; the MAX17043 remains at `0x36`. The Pi has no built-in ADC, so the potentiometer requires an external ADC exposing `/sys/bus/iio/devices/iio:device0/in_voltage0_raw` or it stays centered.

Run the native executable from the Pi with suitable device permissions (usually membership in the `spi`, `i2c`, and `gpio` groups):
`./.pio/build/raspberrypi5/program`

The Pi 5 port does not link ESP32-only libraries: the CC1101 radio uses its own Linux spidev driver (`src/rpi5/cc1101_linux.cpp`, wired through `hal_radio.cpp`) with POCSAG decode, IR expects a configured LIRC device, and NFC talks to the PN532 over I2C.

### Test on a PC (no Pi hardware)

The native binary also runs on any Linux PC or WSL2 — every hardware access fails gracefully. To see and drive the UI from your keyboard:

`python3 tools/pc_harness.py`

It renders the screen to the terminal as ANSI half-blocks and maps keys to buttons/pot (`a/A b/B c/C` = press/release, `,`/`.` or arrow keys = pot, `0-9` = pot set, `q` = quit). Requires an ANSI color terminal. The ESP32 target still needs a device or emulator.

### Static Analysis
To run module checks and ensure memory safety:
`.venv/bin/pio check -e raspberrypi5`

The ESP32 build reports flash/RAM usage automatically. For the native target, use `size .pio/build/raspberrypi5/program` and inspect `/proc/<pid>/status` while it is running.

---

## 6. Current Development Status

The OS architecture, UI framework, and application lifecycles are fully implemented in software. I am currently applying for a grant to acquire the physical components listed in the BOM. 

**Immediate Next Steps Upon Hardware Acquisition:**
* Validate ILI9341 SPI timing and backlight wiring on the Pi 5 hardware.
* Add a Linux CC1101 driver if sub-GHz radio support is required.
* Configure `/dev/lirc0` for IR receive/transmit support.
* Mount the system into the 3D-printed chassis and conduct thermal testing.

## NOTES

As the OS is in development and is far off from being done, theres a lot of future-planned funtions that are never used. I will implement them in the future versions. if you run a [pio check] on your terminal you will see al the planned funtions.

![alt text](<src/UI/assets/Pio Check.png>)

This is the current ram and flash usage consumed by the OS , I plan on reeducing it but tehres a lot of head-space even now.

## License

MIT License