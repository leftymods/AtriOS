# AtriOS AtriStation — накопленные знания (append-only)

## 2026-08-26: LED RING — РАБОТАЕТ, НЕ ТРОГАТЬ
User explicitly said: "ledring работают и запомни это" — IS31FL3236 LED ring
nodes (both i2c0 @0x3c and @0x3f) functioning on device. NEVER modify these.

## Original Yandex Android DTB pin map (decoded from real S905X2 dump)
| Function | Vendor | Was in our DTS |
|---|---|---|
| WL_REG_ON | GPIOX_8 active-high (0x16/0x48) | X7 low (wrong) |
| WiFi HostWake IRQ | GPIOX_9 active-low (0x49) | not wired |
| BT reset | GPIOX_19 (0x53) | X18 (was wrong!) |
| BT hostwake | GPIOX_21 (0x55) | not wired |
| LPO 32k clock | pwm_ef ch0 period 30541 duty~48% (pwm@19000) | pwm_ef correct |
| Knob volume | PUSH A/B selector, periphs idx49 + AO_10 → KEY_VOLUMEUP/DOWN polled | was GPIOA_0+AO10 quadrature |
| Front button | GPIOA_15 code 0x246 = KEY_HOMEPAGE | had KEY_VOICECOMMAND |
| Mic-mute red btn | vendor wires it as 0x40 with code 0xf8 too! gpio-keys not analog? | assumed analog earlier |
| HP detect | 0x4d GPIOH_13 for hp-det-gpio | not wired |
| MAC wifi/bt | stored in eMMC unifykey (mac_wifi/mac_bt via SMC storage_read 0x82000061), NOT chip efuse | we read chip efuse |

## Vendor audio topology (from same dump)
- Card name: AML-AUGESOUND; TDM_B master mclk-fs=256 slots=2 width=32
- slot masks tx-mask=<1 1> rx-mask=<1 1> (slots 0-1 confirmed)
- codecs on one link: woofer(0x2b PBTL) tweeters(0x2a) headphone ES8156(0x08) + loopback es7210@40
- hp-det-gpio present = headphone detection exists on HW

## Build system gotchas (learned the hard way)
1. Commit-pin builds go through cache/patch/kernel-drivers BUNDLE that only
   includes driver subsystems armbian's harness knows. Driver C-file fixes
   in fork commits may be SILENTLY DROPPED unless delivered as userpatch.
2. userpatches/kernel/archive/meson64-6.18/ is applied AFTER all bundles.
3. DTS edits DO survive via kernel pin (DTS is rebuilt from tree).
4. sed on shared strings breaks DTB (duplicate props). Always anchor uniquely.
5. speaker-test needs plughw or asound.conf default→hw:ATRISTATION,0.
6. rtw88 backport patch 001-drivers-net-wireless-realtek-rtw88-upstream-wireless.patch rewrites efuse.c wholesale - any fork efuse edits need userpatch delivery.
