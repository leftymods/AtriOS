# AtriOS — AI session memory

> Дистиллированная память всех сессий разработки. Прочти перед любой
> задачей — здесь ответы на 90% вопросов «почему так сделано» и
> «что уже проверено». Обновляй в конце сессии.

Последнее обновление: 2026-08-24 (сессия: branding + agents + memory)

## 1. Идентификация

- **Плата**: AtriStation = Yandex Station 2, SoC Amlogic S905X3 (SM1),
  family `meson-sm1`, BOARD=`atristation`
- **Цель проекта**: замена облачного стека Яндекса на офлайн-сервисы
- **Ядро**: форк `github.com/leftymods/linux-6.18.y` (branch main),
  пин через `KERNELBRANCH="commit:<sha>"` в
  `config/sources/families/include/meson64_common.inc`
- **SSH-ключ**: `~/.ssh/id_ed25519` (доступ на push в оба репо)
- **AtriOS remote**: переключён с HTTPS на SSH (`git@github.com:leftymods/AtriOS.git`)

## 2. Правила дома (нарушение = злой мейнтейнер)

- **Сборку делает только мейнтейнер**. Агенту — `make`-уровень
  (объекты, хост-бинари), НЕ `./compile.sh build/kernel`
- Патчи ядра запрещены: всё в форк коммитами. Все `.patch` в
  `patch/kernel/archive/meson64-*` выключены — не включать
- `lib/library-functions.sh` и Dockerfile — автогенерация, не править
- При коммите `patch/` — только конкретные файлы, никогда весь каталог
- Коммиты: только когда пользователь попросил

## 3. Окружение (WSL)

| Факт | Значение |
|---|---|
| Хост | DESKTOP-IBU7RPO, user `leftymods`, **без sudo** |
| Кросс-gcc | `/usr/bin/aarch64-linux-gnu-gcc` ✓ |
| shellcheck | `/tmp/opencode/bin/shellcheck` (tmp чистится! бинарник перекачивать) |
| ImageMagick | есть (`magick`), **MSVG не рендерит наш SVG**, `@file` в -draw запрещён политикой |
| PIL/numpy | **нет** (и pip нет) — растеризация чистым python+zlib |
| Рабочий стол Windows | `/mnt/c/Users/ggala/OneDrive/Рабочий стол/` |
| Кэш ядра/uboot | `cache/sources/*-worktree/...` — **владелец root**, не редактировать напрямую |
| /tmp чистится | важные скрипты переносить в репо или /tmp/opencode (тоже ненадёжен) |

## 4. Хронология ключевых решений

### Экран (gowin_led_device, drivers/video/fbdev/)
- JTAG-движок переписан (IEEE 1149.1 + TN653): TCK=H8(MMIO
  `0xff634468` бит 8 — mainline-нумерация; стоковый бит 7 — vendor),
  TMS разделяет SPI-CS (`spi->cs_gpiod[0]`, raw-уровни)
- Прошивка FPGA **асинхронно** при probe (workqueue), пропуск если
  app-протокол отвечает (пустой чип = 00/FF), `force_flash` параметр
- Битстрим встроен: `yandex_fpga_bitstream.h` (~262КБ)
- fb push = **fsync(fd)** (`.fb_sync`), НЕ FBIOBLANK
- TX-буфер преаллоцирован (tx_lock ≠ priv->lock)

### Аудио
- **sy6045s**: regmap-диапазон 0x00–0xB0 (было ≤0x1f — резало 80%
  прошивки); save/restore вокруг firmware УДАЛЁН (дамп сам ставит
  финал: 03=5E, громкости, 22=00 unmute); sysfs amplifier/* рабочие;
  **anti-pop секвенция** с логами `anti-pop: step N` (VDDIO→config→
  forced mute 06=08→PVDD→150ms→unmute по trigger)
- **PVDD-пин = GPIOAO_10** (по оригинальному DTB; X10 из старого
  pastebin — ошибка). Регулятор 20V_AMPL не boot-on
- **ES7210** (порт Armbian rk-6.1-rkr3) и **ES8156** (порт
  rockchip-linux develop-5.10) — адаптированы, W=1 чисто
- Конфликт «uart_AO_B vs TDM-B» был **ложным**: зигби на пинах 2/3
  (uart_ao_b_2_3_pins), аудио на AO4/6/7/8+MCLK AO9

### Zigbee (TZ9213-2782)
- UART_AO_B → `/dev/ttyAML2` (ttyAML1 = **Bluetooth**! serial1=uart_A)
- reset=X17, boot=X11; serdev нет — userspace владеет портом
- Тулза `atri-zigbee`: reset/bootloader/XMODEM-CRC/listen/raw

### Ручка громкости
- Лазерное прерывание → квадратурная микросхема → A/B
- A=GPIOA_0 (periphs offset 49), B=**AO10 — КОНФЛИКТ** с PVDD
- rotary-poll (uinput REL_DIAL) → atrivolume (EMA+гистерезис, дуга
  через сокет atrled с фолбэком)
- **Сейчас ручка ОТКЛЮЧЕНА**: rotary-poll детектит занятый пин,
  пишет «volume knob DISABLED», exit 1 (Restart=on-failure, без спама)
- **Развязка**: `atri-hwprobe --watch-gpio 30` + крутить ручку →
  реальные пины. Если B≠AO10 → одна строка ROTARY_GPIO_B в
  `config/boards/atristation.conf`
- Арбитраж кольца: atrled держит `/run/atriled.override`, atri-main
  передаёт NULL ring в animator_tick

### Конфиг ядра (trim ~2035 символов)
- Хуки в meson64_common.inc: `custom_kernel_config__atristation_trim`
  + `custom_kernel_config__disable_unused_arches`
- **Урок**: BT_HCIUART=n в trim убил детей RTL/3WIRE из opts_y
  (родитель должен быть жив). Симуляция конфига обязательна:
  scripts/config + olddefconfig + grep критичных символов
- Включено обратно: PANFROST (GPU Mali-G31), VIDEO_MESON_VDEC/GE2D,
  VIDEO_DEV, BT_HCIUART+RTL+3WIRE
- ARM_AMLOGIC_CPUFREQ **не существует** в 6.18 — рулит cpufreq-dt
- GPU-стек: альтернативы panfrost нет (vendor DDK мёртв на 6.x)

### Брендинг
- Канонический знак: `docs/assets/atrios-mark.svg` (авторский SVG)
- Рендер: `tools/branding/raster.py` (чистый python) — верифицирован
  AE=0 против закоммиченной марки; **точка рисуется последней**,
  strict `>` в сравнении alpha
- Обои+сплэш: `tools/branding/wallpaper.sh`
- Полная инструкция: `docs/branding.md`
- Сегментированный вариант (v1-ledring) — **proposal на десктопе,
  ждёт решения**; в репо сплошные дуги
- plymouth-пакет: postinst активирует тему **atrios** (было armbian)

## 5. Стоковый DTB (пастбин JL5FHk7u) — расшифровка

Банки: 0x16=periphs (Z0-15,H0-8,BOOT0-15,C0-7,A0-15,X0-19),
0x15=AO(0-11)+E(12-14). Декод-таблица = порядок MESON_PIN в
drivers/pinctrl/meson/pinctrl-meson-g12a.c (SM1 использует его же,
compatible `amlogic,meson-g12a-periphs-pinctrl`).

Ключевые: panel-reset=X9(AL), jtag H5/H6/H7/H8, jtag_sel=E2,
zigbee X17/X11, BT-enable=X18, зигби-UART=AO2/3, аудио=AO4/6/7/8/9,
ampl_pwr=AO10, энкодер A=X? нет — A=GPIOA_0(49), B=AO10.
Полная карта: `docs/atristation-hardware.md`.

⚠ В дампе НЕТ wifi/bt узлов (не тот ревизионный срез или legacy) —
наши X7/X18 из других источников.

## 6. Инструменты (packages/atri-led → /usr/bin)

`atrled` (демон: tweening ~125Гц, перцептивный блендинг через
гамма-LUT, кросфейд, override-флаг) · `atrledctl` (вкл. status) ·
`atrivolume` · `atri-screen-test` · `atri-hwprobe` (+--watch-gpio,
--i2c-read) · `atri-zigbee`. Тесты библиотеки: `make test` в
packages/atri-led.

## 7. Открытые пункты (только железо)

1. Брингап по `docs/atristation-bringup.md`
2. `--watch-gpio` → пины ручки → ROTARY_GPIO_B (или подтвердить конфликт)
3. PVDD: мультиметр после `anti-pop: steps 4-5` (AO10 должен дать 20В)
4. BT wake-линии (X17 занят зигби), UHS WiFi по шагам с iperf3
5. Trim-проход №2 по живому lsmod
6. Прошивки VDEC в рутфс: `dpkg -L firmware-misc-nonfree | grep vdec`

## 8. Грабли-повторения (не наступать дважды)

- `git add -A` в пакете без .gitignore = бинарники в коммит
- `&` в bash-однострочнике бэкграундит ВСЮ цепочку → ctl-тесты мимо;
  фоновые демоны держат шелл → setsid + все три дескриптора в /dev/null|файл
- `pgrep -f` матчит собственную командную строку — ложное «жив»
- Makefile-цели модулей: `snd-soc-x.o` требует `snd-soc-x-y := x.o`
  (забыл = «No rule»); для obj-y цель — просто `x.o`
- kbuild hunk-заголовки в рукописных патчах: неверный счётчик строк
  = git apply молча режет хвост
- olddefconfig с timeout может не дописать auto.conf → «No rule»
- tmp-каталоги чистятся: генераторы — сразу в репо
