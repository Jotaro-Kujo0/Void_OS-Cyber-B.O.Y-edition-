# Void-OS - Cyber B.O.Y. Edition

Void-OS is a custom, an operating system built from scratch for the ESP32 . Designed to operate as a standalone, wearable cyber-deck terminal, it features a custom rendering engine, hardware abstraction architecture, and a modular application ecosystem. Still in development

## Table of Contents
1. [Project Overview](#project-overview)
2. [Software Architecture](#software-architecture)
3. [Application Lifecycle](#application-lifecycle)
4. [Hardware Specifications (BOM)](#hardware-specifications)
5. [Build and Installation](#build-and-installation)
6. [Current Development Status](#current-development-status)

---

## 1. Project Overview

The goal of Void-OS is to provide complete, bare-metal control over a portable hardware stack. Instead of relying on heavy smartwatch operating systems, Void-OS is written in C++ to maximize the dual-core processing capabilities of the ESP32. It prioritizes memory management and direct hardware interfacing.

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
| **Microcontroller** | ESP32 Development Board | Pending |
| **Display** | 2.8-inch TFT Touchscreen (SPI) | Pending |
| **Storage** | SD Card Slot (Integrated into TFT module) | Pending |
| **Chassis** | Custom 3D-Printed Wearable Housing | Pending |
| **Power Supply** | 3.7V LiPo Battery & Charge Controller | Pending |

---

## 5. Build and Installation

Void-OS is managed via PlatformIO. 

### Prerequisites
* VS Code with the PlatformIO extension installed.
* C++ build tools.

### Compilation
1. Clone the repository.
2. Open the project folder in VS Code.
3. Allow PlatformIO to initialize and download dependencies (`TFT_eSPI`, `TinyGPSPlus`, etc.).
4. Run the build command in the PlatformIO CLI:
   `pio run`

### Static Analysis
To run module checks and ensure memory safety:
`pio check`

---

## 6. Current Development Status

The OS architecture, UI framework, and application lifecycles are fully implemented in software. I am currently applying for a grant to acquire the physical components listed in the BOM. 

**Immediate Next Steps Upon Hardware Acquisition:**
* Establish SPI communication between the ESP32 and the 2.8" TFT.
* Validate read/write speeds for asset loading from the integrated SD card.
* Map physical button matrix to the `hal_input` logic.
* Mount the system into the 3D-printed chassis and conduct thermal testing.

## NOTES

As the OS is in development and is far off from being done, theres a lot of future-planned funtions that are never used. I will implement them in the future versions. if you run a [pio check] on your terminal you will see al the planned funtions.

![alt text](<src/UI/assets/Pio Check.png>)

This is the current ram and flash usage consumed by the OS , I plan on reeducing it but tehres a lot of head-space even now.

## License

MIT License