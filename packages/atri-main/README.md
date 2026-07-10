# atri-main

Offline userspace daemon for AtriStation LED matrix and ring.

Replaces the cloud-dependent `maind` from the Yandex Station 2 firmware.
Drives the local SPI framebuffer (`atri_led_panel`), the 24 RGB ring LEDs
(IS31FL3236 via `/sys/class/leds/ring*`), reacts to input events, and plays
offline animations.

No network, cloud, Alice or WebRTC code is included.

## Build

```bash
make
make install DESTDIR=/tmp/stage
```

## Runtime dependencies

- Linux framebuffer device for the LED panel
- `/sys/class/leds/ring*` multicolor LED nodes
- `/dev/input/event*` for volume/mute/function buttons

## Configuration

`/etc/atri-main/main.conf`:

```ini
screen_fb_path=/dev/fb0
backlight_path=/sys/class/backlight/atri_led_panel/brightness
ring_leds_path=/sys/class/leds/ring*
animations_dir=/usr/share/atri-main/animations
default_brightness=200
idle_timeout_ms=30000
volume_timeout_ms=2000
boot_duration_ms=3000
use_syslog=1
```

## Service

```bash
systemctl enable --now atri-main
```
