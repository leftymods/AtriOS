# AtriStation bring-up runbook

Порядок проверки платы после прошивки образа. UART-recovery наготове,
known-good DTB рядом.

## 0. Загрузка

```
dmesg | grep -E "atri_led_panel|gowin|jtag|IDCODE"
```
- `app protocol alive, FPGA already programmed` — быстрый путь, JTAG пропущен
- `programming embedded FPGA bitstream` + `flash written: N pages ... ms` —
  прошилось с нуля (первая загрузка — норма)
- `FAILED: JTAG pins unavailable` / `status poll timeout` — смотреть
  `cat /sys/bus/spi/devices/*/fw_upd_status`

## 1. Шина и устройства

```
i2cdetect -y 2          # ждём: 08 (es8156), 2a/2b (SY6045S), 3c/3f (IS31), 40 (es7210)
atri-hwprobe            # GPIO/I2C/SPI/input/leds/backlight + идентификация
atri-hwprobe --i2c-read # + чтение ID-регистров SY6045S/ES7210
```

## 2. Экран

```
atri-screen-test        # паттерны через fsync->fb_sync->SPI, лог ошибок
dmesg | grep fb_sync    # ошибки пуша кадров
echo 1 > /sys/bus/spi/devices/*/test_fpga_prog   # соук JTAG (до 10 циклов)
```
Кадр отправляется **fsync(fd)** на fb-устройстве, не FBIOBLANK.

## 3. Аудио

```
dmesg | grep -iE "sy6045|es8156|es7210"   # probe без ошибок, applied 117 lines x2
aplay -l && amixer scontrols
speaker-test -D default -c2 -t wav
arecord -f S32_LE -r48000 -c4 -d10 /tmp/pdm.wav   # PDM-микрофоны
```
До первого звука мультиметром: VDDIO≈3.3В, PVDD≈20В (GPIOX_10).

## 4. WiFi/BT (RTL8822CS)

```
dmesg | grep -iE "rtw|8822|mmc1"
iw dev wlan0 scan ; iperf3 -c <host>    # сейчас cap 25МГц SDIO
hciconfig hci0 up ; btmgmt info         # serdev h5, enable=GPIOX_18
dpkg -L firmware-realtek | grep 8822    # rtw8822cs_fw.bin + rtl_bt/*
```
После стабильности включать UHS по одному: sdr25 → sdr50 → sdr104 (+iperf).

## 5. Крутилка (лазерный A/B энкодер)

```
atri-hwprobe            # ищем REL_DIAL устройство от rotary-poll
evtest                  # крутить — события REL_DIAL
systemctl status atrivolume rotary-poll atrled
```
Громкость меняется шагами по 2%, дуга на кольце гаснет через 1.5 c idle.

## 6. Светодиоды

```
atrled list ; atrledctl loop rainbow_wave ; atrledctl volume 70
atrledctl off           # плавное затухание ~170 мс
make -C packages/atri-led test   # юнит-тесты библиотеки (на хосте)
```

## GPU / видео

Mali-G31 MP2 (Bifrost) — драйвер panfrost включён (DRM_PANFROST=y),
userspace — Mesa с panfrost. Аппаратное декодирование видео:
VIDEO_MESON_VDEC, прошивки в /lib/firmware/meson/vdec/g12a_*.
Проверка: dmesg | grep -E "panfrost|mali" ; ls /sys/class/devfreq.

## Zigbee (Tuya TZ9213-2782)

Модуль на UART_AO_B → `/dev/ttyAML2` (стоковые пины AO2/AO3, конфликтов
с аудио TDM-B нет — проверено по оригинальному DTB). Управление:
reset=GPIOX_17, boot=GPIOX_11 (`zigbee-control` в DTS).

```
atri-hwprobe | grep -A2 tty        # порт жив?
atri-zigbee info                   # открытие + ambient-трафик
atri-zigbee listen 30              # сырые байты от модуля
# перепрошивка в координатор:
atri-zigbee bootloader
atri-zigbee send coordinator.bin   # XMODEM-CRC (Z-Stack/EmberZNet NCP)
atri-zigbee raw                    # passthrough для z2m/bellows
```
Стоковая прошивка говорит на протоколе Яндекса; для zigbee2mqtt/ZHA
нужна координаторская прошивка соответствующего чипа.

## Известные ограничения

- BT wake-линии не подключены (GPIOX_17 = zigbee-reset конфликт;
  host-wake GPIOAO_0 не подтверждён трассировкой)
- Zigbee отключён: uart_AO_B делит пины с tdm_ao_b_sclk (аудио TDM-B)
- MAXIO_PHY выключен тримом: если Ethernet не поднимется — вернуть
  CONFIG_MAXIO_PHY=y в hooks
