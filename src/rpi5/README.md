# Raspberry Pi 5 port notes

This directory contains the Linux-native support layer for Void-OS. The target is Raspberry Pi OS on a Raspberry Pi 5, not bare-metal firmware. The normal Linux device permissions are therefore required:

```bash
sudo raspi-config                 # enable SPI and I2C
sudo usermod -aG spi,i2c,gpio $USER
```

Log out and back in after changing group membership.

## Current implementation

- `TFT_eSPI.h/.cpp`: small compatibility class backed by an RGB565 framebuffer and ILI9341 SPI output. It uses `/dev/spidev0.0`, GPIO25 for DC, and GPIO24 for RESET.
- `Arduino.h`: compatibility functions used by the shared scheduler/UI (`millis`, `micros`, `delay`, `Serial`, `ps_malloc`, and basic utility functions).
- `hal_buttons_exp`: PCF8574 reads from `/dev/i2c-1`, address `0x20`.
- `hal_battery`: MAX17043 reads from `/dev/i2c-1`, address `0x36`.
- `hal_gps`: non-blocking `/dev/serial0` reader with GGA/RMC parsing.
- `hal_storage`: binary key/value files under `$XDG_STATE_HOME/void-os` or `$HOME/.local/state/void-os`.
- `cc1101_linux.*`: base Linux SPI transport for a CC1101 on `/dev/spidev0.1`. It is intentionally not connected to the app until a radio profile is selected.
- `pn532_linux.*`: base PN532 I2C frame transport and passive-target polling. It is the starting point for replacing the old NFC stub.

## Features still needed

### CC1101 radio

1. Select and document the regional frequency profile (433, 868, or 915 MHz).
2. Add a complete CC1101 register table for the selected modulation, bitrate, deviation, channel bandwidth, sync mode, and packet length.
3. Configure GDO0 as an interrupt or poll it with the Linux GPIO character-device API.
4. Add RX overflow/TX underflow recovery and packet CRC/error reporting.
5. Protect the shared SPI bus if the TFT and radio can be accessed concurrently.
6. Expose frequency, RSSI, channel scan, transmit, and receive state to `hal_radio.cpp`.
7. Add a legal/regional transmit power limit and never ship a generic unrestricted transmitter profile.

The base class currently provides register access, strobes, FIFO access, and an availability check. It does **not** perform radio configuration or transmit by itself.

### PN532 NFC

1. Finish MIFARE Classic authentication and block read/write support.
2. Validate the PN532 module's I2C address and IRQ/wake wiring; some breakouts require a mode switch before I2C works.
3. Add timeouts and recovery for a removed tag or a wedged I2C controller.
4. Store only deliberately selected tag data; do not silently log credentials or personal data.
5. Connect `Pn532I2c::pollUid()` to `app_nfc.cpp`, then add a bounded persistent tag history.
6. Add tests using captured PN532 response frames, because physical NFC hardware is not available in CI.

### Infrared

1. Choose either `/dev/lirc0` or a GPIO-timing implementation.
2. Add protocol decoding/encoding for the required remotes.
3. Add permissions and a systemd/LIRC setup note.
4. Reuse the existing slot format only after verifying protocol IDs are stable between implementations.

### Display and power

1. Replace the per-frame full-screen SPI flush with dirty rectangles or DMA if the required frame rate is higher than the current 30 FPS.
2. Add a real backlight PWM implementation using the selected Pi 5 PWM channel.
3. Add SIGTERM handling and a clean shutdown path; Raspberry Pi 5 does not have ESP32 deep sleep.
4. Add display orientation/calibration checks for the exact ILI9341 breakout.

### Input and storage

1. Select an external ADC (ADS1115/MCP3008/etc.) and implement its driver instead of the neutral pot fallback.
2. Add rotary encoder quadrature decoding and kill-switch handling.
3. Add atomic file replacement and schema/version migration for persistent settings.
4. Add file permission checks so state files are not world-readable.

## Build and test checklist

```bash
pio run -e raspberrypi5
pio run -e esp32dev
pio check -e raspberrypi5
```

The Raspberry Pi build can also be run without PlatformIO using the command in the repository README. Hardware tests should verify, in order:

1. `ls -l /dev/spidev0.0 /dev/i2c-1`.
2. `i2cdetect -y 1` shows `0x20`, `0x24`, and `0x36` for the connected modules.
3. The TFT reset/DC signals are wired to the BCM pins documented in `config.h`.
4. The display starts in framebuffer-only mode if SPI permissions are missing, making UI development possible without hardware.
5. Buttons, battery readings, GPS fix state, and each optional driver are tested independently before enabling them in the app registry.
