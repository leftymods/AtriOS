# AtriStation — карта железа (S905X3 / SM1)

Сверено с оригинальным DTB Яндекса. Глобальные номера GPIO = смещение
в банке; bank base смотреть в `atri-hwprobe`.

## Периферия по банкам

| Пин | Функция | Направление | Примечание |
|---|---|---|---|
| **GPIOH_5** | JTAG TDI (панель) | out | бит-банг прошивки FPGA |
| **GPIOH_6** | JTAG TDO (панель) | in | также uart_C RX — занят |
| **GPIOH_7** | JTAG TMS ≡ SPI1 CS0 | both | общий с экраном, raw-уровни в JTAG |
| **GPIOH_8** | JTAG TCK (панель) | out | MMIO: `0xff634468` бит 8 |
| **GPIOX_7** | WiFi reset (sdio-pwrseq WL_REG_ON) | out | vendor DTB: 0x48 (72 dec = GPIOX_7), alt uart_B RX |
| **GPIOX_9** | Reset SPI-экрана | out, active-low | vendor DTB: 0x4a (74 dec = GPIOX_9) |
| **GPIOX_10** | Enable 20V усилителей | out, active-high | vendor DTB: 0x4b (75 dec = GPIOX_10) |
| **GPIOX_11** | Zigbee boot | out | vendor DTB: 0x4c (76 dec = GPIOX_11) |
| **GPIOX_17** | Zigbee reset | out | vendor DTB: 0x52 (82 dec = GPIOX_17) |
| **GPIOX_19** | Bluetooth enable (BT_EN) | out | vendor DTB: 0x53 (83 dec = GPIOX_19) |
| **GPIOAO_3** | Zigbee UART RX | in | ⚠ бывший tflash_vdd — отключён |
| **GPIOAO_8** | TDM-B SCLK (аудио) | out | ⚠ uart_AO_B вариант 8/9 недоступен |
| **GPIOAO_9** | TDM-B MCLK (аудио) | out | |
| **GPIOAO_4/6/7** | TDM-B DIN0/DOUT2/FS | — | усилители + ES7210 |
| **GPIOAO_10** | Энкодер фаза B | in | rotary-poll (лазер A/B) |
| **GPIOA_0** | Энкодер фаза A | in | |

## UART

| Порт | Устройство | Пины | Владелец |
|---|---|---|---|
| uart_AO | ttyAML0 | AO(консольные) | console |
| uart_A | ttyAML1 | X12/X13? (BT serdev) | hci_h5 + btrtl |
| uart_AO_B | ttyAML2 | **AO2(TX)/AO3(RX)** | Zigbee TZ9213-2782 |
| uart_B | — | X6/X7 | свободен частично (X7 занят WiFi-reset) |
| uart_C | — | H6/H7 | занят панелью/JTAG |

## I2C (шина 2 → /dev/i2c-N, см. atri-hwprobe)

| Адрес | Чип | Роль |
|---|---|---|
| 0x08 | ES8156 | внешний I2S DAC |
| 0x2a | SY6045S | твитеры |
| 0x2b | SY6045S | вуфер (**PBTL**, регистр 0x20=0x80 в дампе) |
| 0x3c / 0x3f | IS31FL3236 | LED-кольцо (2 контроллера) |
| 0x40 | ES7210 | 4-канальный ADC (микрофоны/feedback) |

Прошивки SY6045S: `/lib/firmware/sy6045s-{tweeters,woofer}-settings.txt`
(117 записей `w <addr7<<1> <reg> <val...>`, применяются драйвером при probe).

## Экран (FPGA Gowin GW1N-4B)

- SPI1 @4МГц, CS=GPIOH_7, compatible `atri,led-panel`
- Кадр: `WRITE len + W*H байт` + `SHOW_PIC`; push = **fsync(fb_fd)**
- Прошивка: встроена в драйвер (`yandex_fpga_bitstream.h`), шьётся
  асинхронно при probe; если app-протокол отвечает — пропускается
- Статус: `/sys/bus/spi/devices/*/fw_upd_status`, отладка: `jtag_codes`,
  соук: `echo N > test_fpga_prog`

## Загрузка (seamless)

```
U-Boot: bmp display /boot/boot.bmp (1280x720)
   ↓ VIDEO_DT_SIMPLEFB → /chosen/simple-framebuffer
Linux: FB_SIMPLE держит картинку до DRM_MESON
   ↓
plymouth (тема atrios) → рабочий стол/даун
```

## Защита от щелчка (anti-pop) — реализована в драйвере sy6045s

Секвенция при probe (каждый шаг в dmesg с префиксом `anti-pop:`):

```
1. VDDIO on                    — I2C жив, выходная ступень мертва
2. reset → restore-regs → firmware (EQ/DRC)
3. принудительный mute (reg06=0x08, проверяется обратным чтением!)
   ← дамп настроек сам кончается UNMUTED, поэтому глушим принудительно
4. PVDD on (20V_AMPL)          — заряд выходной ступени В МЮТЕ
5. settle 150 ms
6. unmute только по trigger(START) — т.е. когда I2S-такт уже идёт
```

При remove — наоборот: mute → PVDD off → VDDIO off.

✅ Пин PVDD подтверждён по оригинальному DTB: **GPIOX_10** (`0x16 0x4b 0x00`,
75 dec в meson-g12a-gpio.h). Наш регулятор 20V_AMPL привязан к нему; anti-pop
секвенция драйвера sy6045s подаёт его шагом 4.

Линия **GPIOAO_10** (`0x15 0x0a 0x00`) полностью свободна и является
аппаратной фазой B энкодера громкости. Конфликта между ручкой громкости и
питанием усилителей нет.

## Лазерный энкодер громкости

Механика: лазер прерывается диском с прорезями → квадратурная
микросхема формирует два сигнала A/B со сдвигом 90° → они приходят
на **GPIOA_0 (фаза A)** и **GPIOAO_10 (фаза B)**.

В DTS оба пина объявлены в узле `rotary-volume` (compatible `rotary-volume` под драйвер ядра `CONFIG_INPUT_ROTARY_VOLUME=m`):
- Фаза A: `GPIOA_0`
- Фаза B: `GPIOAO_10`

Квадратурный декодер таблицы состояний Грея в ядре транслирует шаги вращения в стандартные input-события:
`KEY_VOLUMEUP` и `KEY_VOLUMEDOWN`.

## Про vendor-jtag и прошивку экрана

Отключённый узел `jtag { compatible = "amlogic,jtag"; status=disabled }`
— это **CPU-отладочный JTAG** (мультиплексор отладочного порта SoC,
select="apao"). К прошивке FPGA экрана он отношения не имеет:
экран шьётся собственным бит-бэнг JTAG драйвера gowin_led_device по
выделенным ногам H5/H6/H7/H8 (+CS), полностью независимо. Он
работает: асинхронно при probe, либо пропускается если FPGA уже
отвечает по app-протоколу (`fw_upd_status: skipped`).

## Аппаратный статус подсистем

- **WiFi**: SDIO RTL8822CS на `sd_emmc_a`, питание `WL_REG_ON` = **GPIOX_7** (ACTIVE_LOW в simple-pwrseq), тактирование `wifi32k` (32.768 кГц через PWM_E).
- **Bluetooth**: serdev на `uart_A`, включение `BT_EN` = **GPIOX_18**, линия `host-wake` = **GPIOX_19**.
- **Zigbee**: UART_AO_B (AO2/AO3), reset = **GPIOX_17**, boot = **GPIOX_11**.
- **Звук**: SY6045S твитеры (0x2a) + вуфер (0x2b, PBTL) + ES8156 ЦАП (0x08) на I2C2 (Z-банк), PVDD 20V через **GPIOX_10**.
- **Экран**: SPI1 (spicc1) Gowin GW1N-4B, CS = GPIOH_7, Reset = **GPIOX_9**, JTAG на GPIOH_5..8, BOOT_0, GPIOE_2.
- **LED-кольцо**: I2C0, 2x IS31FL3236 (0x3c, 0x3f).
