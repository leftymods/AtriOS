# AtriOS

Offline replacement firmware for **Yandex Station 2** (AtriStation,
Amlogic S905X3 / SM1). Replaces the cloud stack with local services:
LED ring + SPI matrix screen, laser volume knob, Zigbee radio,
audio pipeline (SY6045S amps + ES8156 + ES7210 + PDM mics),
WiFi/BT (RTL8822CS), seamless U-Boot → kernel boot splash.

## Build

```bash
./compile.sh build BOARD=atristation BRANCH=current BUILD_MINIMAL=yes
```

Single entrypoint; everything else is `KEY=VALUE`. See `AGENTS.md`
for conventions.

## Documentation

| Doc | Content |
|---|---|
| [`docs/atristation-bringup.md`](docs/atristation-bringup.md) | bring-up checklist per subsystem (screen/audio/wifi/bt/knob/zigbee) |
| [`docs/atristation-hardware.md`](docs/atristation-hardware.md) | pin map decoded from stock DTB, anti-pop sequencing, open questions |
| `AGENTS.md` | repo conventions for agents/builders |

## Board tooling (installed to /usr/bin)

| Tool | Purpose |
|---|---|
| `atrled` / `atrledctl` | LED ring daemon (tweened animations, arcs) + control |
| `atrivolume` | laser-knob volume → ALSA + ring arc |
| `atri-screen-test` | LED matrix test suite with verbose error log |
| `atri-hwprobe` | enumerate GPIO/I2C/SPI/tty/LEDs + identify chips; `--watch-gpio` reveals unknown pins by motion |
| `atri-zigbee` | Zigbee module reset/bootloader/XMODEM flash/passthrough |

## Kernel

Custom fork [`leftymods/linux-6.18.y`](https://github.com/leftymods/linux-6.18.y),
pinned by `KERNELBRANCH` in
`config/sources/families/include/meson64_common.inc`.

Board-specific drivers: `gowin_led_device` (SPI screen + FPGA JTAG
flash), `sy6045s` (amps, anti-pop sequencing), ported `es7210` /
`es8156`; GPU via panfrost, video decode via meson-vdec.
