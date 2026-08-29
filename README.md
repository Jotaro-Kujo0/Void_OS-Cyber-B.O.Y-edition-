# Void-OS - Cyber B.O.Y. Edition

Cyber-B.O.Y is a cyberdeck project inspired by pip-boy from fallout, this repository contains schematics for the hardware and the software for this project.
Software is a sub-repo of my VoidOS project.

---

## 1. Project Overview

The goal of Void-OS is to provide direct control over a portable hardware stack without a heavy GUI framework. On Raspberry Pi 5, the native target uses Linux SPI (`/dev/spidev0.0`), I2C-1, the GPIO character-device API, POSIX serial GPS parsing, and file-backed settings. Its meant to be used for testing out the security of devices around 
and learning about ethical hacking

---

## 2. Software Architecture

Void-OS is built on a strict separated parts, divided into three main layers:

### The Hardware Abstraction Layer (HAL)
To ensure the OS can be ported to different board layouts in the future, all physical components are managed through dedicated HAL modules:
* `hal_display`: Manages SPI communication and pixel-pushing to the TFT screen.
* `hal_input`: Processes physical inputs (buttons, rotary encoders, potentiometers).
* `hal_power` & `hal_battery`: Monitors power draw, battery levels, and screen dimming states.
* `hal_gps` & `hal_radio`: Interfaces with external communication and location modules.


### The UI Framework
A custom, lightweight graphics engine handles all screen drawing. It bypasses standard heavy GUI libraries in favor of optimized, direct-to-buffer rendering functions (`draw_rect`, `draw_text`, `draw_sprite`) to ensure high framerates on the 2.8-inch display.

###  App System
Applications are siloed from the core OS logic. They are bare-bone at the moment but I'll expand them with new releases


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
| **Compute** | Raspberry Pi 5 | First iteration is strict for these, will provide new microcontrollers in the future|
| **Display** | 2.8-inch ILI9341 SPI TFT, 240x320/ 3.5" ressisve touch | Supported |
| **Input** | PCF8574 buttons over I2C; optional external IIO ADC for pot | Supported on Pi 5 | Can be used with any other standart switch
| **Storage** | Raspberry Pi XDG state files | Supported | Storage needs an SD card strictly tho
| **Power Supply** | 3.7V Li-ion battery & charge controller | Hardware-dependent | Will be working on a more reliable version

---

## 5. Build and Installation

Void-OS is managed via PlatformIO. Can use CMakeLists for testing. 

### Prerequisites
* VS Code with the PlatformIO extension installed.
* C++ build tools.
* Python 3.10+ and a virtual environment.

### Compilation
1. Clone this repository.
2. Create the local build environment:
   `python3 -m venv .venv && .venv/bin/pip install -r requirements.txt`
3. Install the declared PlatformIO platforms, frameworks, and libraries:
   `.venv/bin/pio pkg install -e esp32dev`
   `.venv/bin/pio pkg install -e raspberrypi5`
5. Build the Raspberry Pi 5 native executable:
   `.venv/bin/pio run -e raspberrypi5`

### Raspberry Pi 5 wiring

Use BCM numbering. Enable SPI and I2C with `raspi-config`, then connect the ILI9341 to SPI0 CE0 (`/dev/spidev0.0`), DC to GPIO25, RESET to GPIO24, and connect the PCF8574/MAX17043 to I2C-1 (GPIO2 SDA, GPIO3 SCL). The PCF8574 remains at address `0x20`; the MAX17043 remains at `0x36`. The Pi has no built-in ADC, so the potentiometer requires an external ADC exposing `/sys/bus/iio/devices/iio:device0/in_voltage0_raw` or it stays centered.

Run the native executable from the Pi with suitable device permissions (usually membership in the `spi`, `i2c`, and `gpio` groups):
`./.pio/build/raspberrypi5/program`

The Pi 5 port does not link ESP32-only libraries: the CC1101 radio uses its own Linux spidev driver (`src/rpi5/cc1101_linux.cpp`, wired through `hal_radio.cpp`) with POCSAG decode, IR expects a configured LIRC device, and NFC talks to the PN532 over I2C.

### Test on a PC (no Pi hardware)

The native binary also runs on any Linux PC or WSL2 — every hardware access fails gracefully. To see and drive the UI from your keyboard:

`python3 tools/pc_harness.py`

It renders the screen to the terminal as ANSI half-blocks and maps keys to buttons/pot (`a/A b/B c/C` = press/release, `,`/`.` or arrow keys = pot, `0-9` = pot set, `q` = quit). Requires an ANSI color terminal. The ESP32 target still needs a device or emulator.

For the same live view in a browser (no ANSI terminal needed):

`python3 tools/web_harness.py`

It auto-builds the native binary (via PlatformIO if present, else the CMake build) and serves `http://127.0.0.1:8000/`. The page is a desktop: 12 soft-corner square app icons on each side (spaced out), my big gear-headed character in the centre  (his name is LV. and is the mascot for my project), and click-to-open draggable, wide windows. Windows are rendered in the browser with HTML/CSS/JS — the HELP app ships a full cheatsheet, the other apps show dummy device stats. Clicking an icon also sends the firmware a `launch <NAME>` REPL command so the device stays in sync. Close a window with the ✕ or Esc. The serial log lives in a taskbar drawer, and the keyboard still drives the device directly (`.`, `,`, arrows and `0-9` nudge/set the pot, `a/A b/B c/C` press/release, `q` quits, 0*9 may not work on first version). Device stats and logs stream over a WebSocket by default; if the browser or network blocks WebSockets the page automatically falls back to plain-HTTP polling (`/poll`, `/cmd`), so the desktop works anywhere the page loads. Options: `--port N`, `--every N` (thin the firmware's PPM writes), `--fps N` (1–60, default 15 — caps the encode+broadcast rate), `--build`, `--binary PATH`. It reuses the same `VOIDOS_HARNESS`/`VOIDOS_DUMP_*` hooks as the PC harness, so no firmware changes are needed.



### Static Analysis
To run module checks and ensure memory safety:
`.venv/bin/pio check -e raspberrypi5`

---

## 6. Current Development Status

The OS architecture, UI framework, and application lifecycles are fully implemented in software. Its far from finished but gives the basic idea (hopefully)

## NOTES

As the OS is in development and is far off from being done, theres a lot of future-planned fuctions that are never used. I will implement them in the future versions. if you run a [pio check] on your terminal you will see al the planned funtions.

![alt text](<src/UI/assets/Pio Check.png>)

This is the current ram and flash usage consumed by the OS , I plan on reeducing it but tehres a lot of head-space even now.

You can see more detail inside the code, I left comments that hopfully make people undertsand the architecture.
