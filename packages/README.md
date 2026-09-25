# AtriOS Hardware Packages

| Package | Function & Scope |
|:--|:--|
| **`atri-led`** | **24-Zone RGB LED Ring Only** (`IS31FL3236`): `atri-led` daemon, `atri-led-ctl` CLI, `libatri_led.a`, and animations. |
| **`atri-display`** | **Front 25×16 LED Screen**: `atri-matrix` (tests, demos, text), `atri-displayd` (clock, eyes, temp, IP daemon), and `atri-display` CLI. |
| **`atri-audio`** | **Audio Subsystem**: `atri-sound-test` (audit, 440/80Hz tones, sweep, 4-mic live VU-meter) and `atrivolume` (laser knob encoder daemon). |
| **`atri-wireless`** | **Connectivity & Radio**: `atri-onboard` (Bluetooth phone setup), `atri-wifi-diag` (RTL8822CS suite), `atri-wireless-init` (eFuse/MAC), and `atri-zigbee`. |
| **`atri-tools`** | **Platform Management**: Umbrella `atri` CLI, `atri-tui` (`atri menu`, `atri setup`), `atri-hwprobe`, and `atri-autobrightness` (LTR-308ALS). |
| **`atri-main`** | **Offline System Event Daemon**: Screen/ring event coordinator replacing legacy vendor daemon. |
| **`atri-fw`** | **Firmware Blobs**: RTL8822CS calibrated eFuses, FPGA bitstreams, and Zigbee NCP images. |
| **`bsp`** | Common board support scripts and system configurations. |
