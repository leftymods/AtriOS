<p align="center">
  <img src="docs/assets/atrios-logo.png" alt="AtriOS Logo" width="760"/>
</p>

<p align="center">
  <strong>Autonomous Embedded Linux Distribution for Smart Audio Hardware & Displays</strong>
  <br />
  <em>Complete local, cloud-free replacement firmware for <strong>Yandex Station Max / AtriStation</strong> (Amlogic S905X2 / S905X3 / SM1)</em>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Architecture-ARM64%20(aarch64)-0284c7?style=flat-square" alt="Architecture" />
  <img src="https://img.shields.io/badge/Linux%20Kernel-6.18.y%20(Mainline%20Fork)-7c3aed?style=flat-square" alt="Kernel" />
  <img src="https://img.shields.io/badge/SoC-Amlogic%20SM1%20%2F%20G12A-ea580c?style=flat-square" alt="SoC" />
  <img src="https://img.shields.io/badge/Audio-24--bit%20Hi--Fi%20%7C%20SY6045S-16a34a?style=flat-square" alt="Audio" />
  <img src="https://img.shields.io/badge/Wireless-RTL8822CS%20Wi--Fi%20%26%20BT-blue?style=flat-square" alt="Wireless" />
  <img src="https://img.shields.io/badge/Hardware%20Testing-Untested%20on%20Physical%20Device-amber?style=flat-square" alt="Hardware Testing" />
  <img src="https://img.shields.io/badge/License-GPL--2.0-slate?style=flat-square" alt="License" />
</p>

> [!WARNING]
> **Experimental / Work in Progress / Не проверено на реальном железе**:
> Прошивка, модификации ядра Linux 6.18, DTS и виртуальный eFuse для RTL8822CS находятся в стадии активной разработки и **пока не были протестированы на физическом реальном устройстве (Yandex Station Max)**. Используйте с осторожностью при первом включении и отладке (bring-up).
>
> *(Firmware, kernel patches, and device-tree modifications are experimental and have **not yet been verified on real physical hardware**).*

---

## Overview

**AtriOS** is an open-source, local-first Linux operating system engineered specifically for **Yandex Station Max (AtriStation)** hardware. It completely replaces the proprietary vendor Android software and cloud stack with a high-performance, open Debian/Ubuntu-based mainline Linux environment.

All hardware features — from the Gowin FPGA LED matrix display and dual IS31FL3236 LED ring to the 20V Silergy SY6045S Hi-Fi amplifiers, 4-channel microphone array, laser volume knob, Zigbee 3.0 NCP, and Realtek RTL8822CS dual-band wireless — operate **100% locally and offline** with zero telemetry or remote vendor lock-in.

---

## Hardware & Subsystem Status

| Subsystem | Hardware Component | Bus / Interface | Driver / Daemon | Status |
|---|---|---|---|---|
| **LED Matrix Display** | Gowin GW1N FPGA (25×16 LED matrix) | SPI (`spicc1`) + JTAG GPIO | `gowin_led_screen` / `atri-matrix` / `atri-displayd` | **Supported** |
| **LED Ring** | Dual IS31FL3236 (24 multicolor RGB zones) | I2C (`i2c0` @ `0x3c`, `0x3f`) | `leds-is31fl32xx` / `atri-led` (`atrled`) | **Supported** |
| **Speaker Amplifiers** | Silergy SY6045S (PBTL Woofer + Stereo Tweeters) | I2C (`i2c2` @ `0x2a`, `0x2b`) + TDM B | `snd-soc-sy6045s` / `atri-sound-test` / PipeWire 2.1 | **Supported** |
| **Headphone DAC** | Everest ES8156 (3.5mm AUX line out) | I2C (`i2c2` @ `0x08`) + TDM B | `snd-soc-es8156` (mainline ASoC) | **Supported** |
| **Microphone ADC** | Everest ES7210 4-channel AEC reference ADC | I2C (`i2c2` @ `0x40`) + TDM B | `snd-soc-es7210` (acoustic echo ref) | **Supported** |
| **Digital Mic Array** | 4-channel PDM microphone array | Amlogic PDM controller | `dmic-codec` / `pdm` DAI link / WebRTC AEC | **Supported** |
| **Wi-Fi** | Realtek RTL8822CS (802.11ac 2×2 Dual-Band) | SDIO (`sd_emmc_a`, SDR50 100MHz) | `rtw88_8822cs` + virtual eFuse loader | **Supported** |
| **Bluetooth** | Realtek RTL8822CS Bluetooth 5.0 (H5) | UART_A (`/dev/ttyAML1`, 3MBaud, RTS/CTS) | `hci_h5` / `btrtl` serdev (`atri-onboard`) | **Supported** |
| **Volume Knob** | Laser quadrature rotary encoder | Polled GPIO input (`REL_DIAL`) | `rotary_volume` / `atrivolume` | **Supported** |
| **Zigbee 3.0** | Tuya TZ9213-2782 / Silicon Labs EFR32 | UART_AO_B (`/dev/ttyAML2`) + GPIOs | `atri-zigbee` (Z2M / ZHA coordinator) | **Supported** |
| **Light Sensor** | Lite-On LTR-308ALS ambient light sensor | I2C (`i2c0` / `i2c2` @ `0x53`) | `ltr308als01` / `atri-autobrightness` | **Supported** |
| **RTC (Real-Time Clock)** | NXP PCF8563 real-time clock | I2C (`i2c2` @ `0x51`) | `rtc-pcf8563` (`/dev/rtc0`) | **Supported** |
| **GPU / Video** | ARM Mali-G31 MP2 + Amlogic VDEC | PCIe / System bus | Panfrost DRM + Meson VDEC (4K HW) | **Supported** |

> *Примечание: драйверы, DTS и модули интегрированы в дерево сборки, однако требуют валидации непосредственно на физическом образце устройства.*

---

## Built-in CLI Tooling & Daemons

AtriOS packages high-performance, native C utilities and system services installed into `/usr/bin`:

### Unified Platform CLI & Interactive TUI
- **`atri`**: Single umbrella management command uniting all system functions:
  ```bash
  atri status             # Comprehensive status: SoC, audio, display, sensors, wireless
  atri menu               # Interactive TUI configurator (Wi-Fi, Bluetooth, Audio, LEDs)
  atri clock / atri eyes  # Switch front screen to digital clock or expressive eyes
  atri btaudio [on|off]   # Toggle Bluetooth A2DP wireless speaker mode
  atri tz [timezone]      # Query or set system timezone
  atri lang [ru|en]       # Query or set system locale
  atri sound / matrix ... # Direct pass-through to subsystem tools
  ```
- **`atri-tui`** (alias **`atri menu`**, **`atri-setup`**): Full interactive ANSI terminal configurator with arrow-key navigation (`[↑/↓]`, `[ENTER]`, `[ESC/q]`):
  - 📱 **Phone Onboarding**: Stealth BLE UUID `0xFE33` or visible test mode toggle
  - 🎵 **Bluetooth Speaker**: A2DP audio sink mode for music streaming from smartphone
  - 📶 **Wi-Fi Manager**: Scan networks and connect via NetworkManager
  - 🔊 **Interactive Volume Slider**: Visual level bar, `[+]`/`[-]` steps, instant test tone
  - 🔊 **Audio Diagnostics**: 440 Hz tweeters, 80 Hz sub-bass, frequency sweep, 4-mic live VU-meter
  - 🖥️ **Screen Modes**: Digital clock (HH:MM), expressive blinking eyes, temperature, IP ticker
  - 💡 **LED Ring & Matrix**: Color selection, Pong demo, pixel test, rainbow animations
  - ☀️ **Auto-Brightness**: Real-time ALS lux monitoring, auto-off in darkness (< 2 lux)
  - 🌐 **Locale & Timezone**: One-click language switch (ru_RU / en_US), NTP time sync
  - 🐝 **Zigbee 3.0**: Radio monitor, firmware flash, hardware reset

### Hardware & Peripherals (Pure C99/POSIX)
- **`atri-onboard`** (alias **`atri-phone-setup`**): Pure C Bluetooth onboarding service using Linux kernel sockets (`AF_BLUETOOTH`, `BTPROTO_RFCOMM`). Operates in stealth BLE advertisement mode (Service UUID `0xFE33`) for companion apps by default, with toggleable visible Classic BT test mode (`--visible`).
- **`atri-sound-test`**: Audio subsystem verification, I2C bus audit (SY6045S, ES8156, ES7210), 20V rail check, sine tones, and live 4-channel microphone array VU-meter.
- **`atri-matrix`**: Consolidated 25×16 Gowin FPGA screen tool (pixel diagnostics, Pong demo, fire/stars animations, text rendering, backlight control).
- **`atri-display` / `atri-displayd`**: Front screen display daemon with 4×10 digital clock font, animated expressions, volume popup overlays, and UNIX socket IPC (`/run/atri-display.sock`).
- **`atri-autobrightness`**: Ambient light adaptation daemon for LTR-308ALS with exponential smoothing and dark-room sleep mode.
- **`atri-led` / `atri-led-ctl`**: Daemon and CLI client for the 24-zone IS31FL3236 RGB ring (color, pulse, rainbow, notifications).
- **`atrivolume`**: Volume control daemon linking rotary encoder (`REL_DIAL`) to ALSA mixer levels with smooth LED ring arc feedback.
- **`atri-hwprobe`**: Consolidated hardware audit tool (PCBA EEPROM parser, ALS lux probe, button monitor, rotary knob tracer).
- **`atri-zigbee`**: Tuya/EFR32 module manager (hardware reset, bootloader, XMODEM-CRC firmware flash, sniff mode).

### Wireless & Diagnostic Suite
- **`atri-wifi-diag`** (alias **`atri-wireless`**): Native C diagnostic utility for Realtek RTL8822CS Wi-Fi and Bluetooth.
  ```bash
  atri-wifi-diag --all            # Full diagnostic suite (Wi-Fi, eFuse, BT, Scan)
  atri-wifi-diag --wifi           # Wi-Fi SDIO bus, clock, and driver status
  atri-wifi-diag --bt             # Bluetooth UART_A serdev, HCI state, and inquiry
  atri-wifi-diag --scan           # Active Wi-Fi scan benchmark (latency, BSSID table)
  atri-wifi-diag --efuse          # Inspect eFuse calibration map (RTL_ID, RFE, Xtal)
  atri-wifi-diag --inject-efuse   # Generate and write calibrated virtual eFuse file
  atri-wifi-diag --json           # Machine-readable JSON output for automated testing
  ```

---

## Building AtriOS

AtriOS uses an optimized, reproducible build framework based on Armbian:

```bash
# Clone the repository
git clone https://github.com/leftymods/AtriOS.git
cd AtriOS

# Build minimal firmware image for AtriStation (Amlogic S905X3 / SM1)
./compile.sh build BOARD=atristation BRANCH=current BUILD_MINIMAL=yes

# OR build for Yandex Station Max (Amlogic S905X2 / G12A)
./compile.sh build BOARD=stationmax BRANCH=current BUILD_MINIMAL=yes
```

### Build Parameters
- `BOARD=atristation`: Target board configuration for Amlogic SM1 (`config/boards/atristation.conf`).
- `BOARD=stationmax`: Target board configuration for Amlogic G12A (`config/boards/stationmax.conf`).
- `BRANCH=current`: Kernel version 6.18.y (`leftymods/linux-6.18.y`).
- `BUILD_MINIMAL=yes`: Clean, lightweight installation without unnecessary desktop bloat.

The compiled bootable image and Debian packages will be produced in `output/images/`.

---

## Documentation

- [`docs/atristation-bringup.md`](docs/atristation-bringup.md): Subsystem bring-up runbook and verification sequence.
- [`docs/atristation-hardware.md`](docs/atristation-hardware.md): Complete pinout map, power rail sequencing, and hardware specifications.
- [`docs/branding.md`](docs/branding.md): Official brand assets, design rules, and logo regeneration instructions.
- [`AGENTS.md`](AGENTS.md): Repository conventions, kernel patching architecture, and guidelines.

---

## License

AtriOS is licensed under the [GNU General Public License v2.0](LICENSE). Individual device drivers, kernel patches, and user utilities retain their respective upstream licenses.
