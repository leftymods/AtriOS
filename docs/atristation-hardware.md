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
| **GPIOX_7** | WiFi reset (sdio-pwrseq) | out | также uart_B RX — занят |
| **GPIOX_9** | Reset SPI-экрана | out, active-low | |
| **GPIOX_10** | Enable 20V усилителей | out, active-high | regulator-boot-on |
| **GPIOX_11** | Zigbee boot | out | загрузчик модуля |
| **GPIOX_17** | Zigbee reset | out | ⚠ не дублировать BT device-wake |
| **GPIOX_18** | Bluetooth enable | out | rtl8822cs-bt |
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

## Известные неразрешённые пункты

- BT wake-линии не подключены (X17 конфликтует с zigbee-reset,
  второй кандидат AO0 не подтверждён трассировкой)
- UHS-режимы SDIO выключены до замеров (cap 25 МГц)
- Чип Zigbee-модуля TZ9213-2782 не идентифицирован по открытым базам;
  определить на железе через `atri-zigbee listen`
