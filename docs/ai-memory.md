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
- **sy6045s**: regmap-диапазон 0x00–0xB0; anti-pop секвенция с
  аппаратным DRC и защитой динамиков на 20V шине; UCM2 HiFi и VoiceCall
  профили с активными playback switches твитеров и вуфера.
- **Строго без TAS**: Texas Instruments кодеков нет на плате физически.
  Используются: SY6045S твитеры (@0x2a), SY6045S вуфер (@0x2b),
  Everest ES8156 ЦАП (@0x08), Everest ES7210 4-ch АЦП (@0x40).
- **Автозагрузка кодеков**: `snd-soc-sy6045s`, `snd-soc-es8156`,
  `snd-soc-es7210` в `/etc/modules-load.d/sound.conf` исключают
  блокировку ASoC DAI link (`-EPROBE_DEFER`).
- **PipeWire 2.1 Crossover & WebRTC AEC**: виртуальный 2.1 кроссовер
  160 Гц (Linkwitz-Riley Lowpass/Highpass) и модуль эхоподавления AEC.
- **PVDD-пин = GPIOX_10** (board owner подтвердил; AO_10 освобождён
  для энкодера ручки громкости). Регулятор 20V_AMPL.

### Zigbee (TZ9213-2782)
- UART_AO_B → `/dev/ttyAML2` (ttyAML1 = **Bluetooth**! serial1=uart_A)
- reset=X17, boot=X11; serdev нет — userspace владеет портом
- Тулза `atri-zigbee`: reset/bootloader/XMODEM-CRC/listen/raw

### Ручка громкости
- Лазерное прерывание → квадратурная микросхема → A/B
- A=GPIOA_0 (periphs offset 49), B=GPIOAO_10 (конфликт с PVDD исчерпан
  переносом 20V_AMPL на GPIOX_10).
- Энкодер: `atrivolume` (EMA+гистерезис, дуга через сокет atrled) и
  ядерный `rotary-volume` / `atri-hwprobe knob`.
- Арбитраж кольца: atrled держит `/run/atriled.override`, atri-main
  передаёт NULL ring в animator_tick.

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

## 6. Инструменты (packages/atri-* → /usr/bin)

- `packages/atri-led`:
  - `atri-led` (симлинк `atrled`) — RGB кольцо (tweening ~125Гц, перцептивный блендинг)
  - `atri-led-ctl` (симлинк `atrledctl`) — CLI кольца
- `packages/atri-display`:
  - `atri-displayd` & `atri-display` — демон и CLI экранного интерфейса
  - `atri-matrix` (объединил 7 утилит: `test`, `demo`, `text`, `on`/`off`, `brightness`, `clear`/`fill`, `info`)
- `packages/atri-audio`:
  - `atrivolume` (симлинк `atri-volume`) — отслеживание громкости и энкодера
  - `atri-sound-test` (симлинк `atrisound-test`) — диагностика звука, синус-тоны, sweep, 4-ch микрофоны
- `packages/atri-wireless`:
  - `atri-zigbee` — прошивка и управление координатором Tuya TZ9213
  - `atri-wireless-init` & `atri-wifi-diag` — автоинициализация MAC/eFuse и диагностика WiFi
  - `atri-onboard` (симлинк `atri-phone-setup`) — BLE онбординг через мобильное приложение
- `packages/atri-tools`:
  - `atri` — umbrella CLI для всех подсистем станции
  - `atri-tui` (симлинки `atri-setup`, `atri-menu`) — консольный интерфейс настройки
  - `atri-hwprobe` (объединил 4 утилиты: `pcba`, `als`, `buttons`, `knob`, сканер I2C/SPI)
  - `atri-autobrightness` — авторегулировка яркости по датчику освещенности

## 7. Открытые пункты (только железо)

1. Брингап по `docs/atristation-bringup.md`
2. PVDD: замер мультиметром на 20V_AMPL (GPIOX_10)
3. Проверка звука через `atri-sound-test` (sweep + 4-ch mic)
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
- Динамические библиотеки (-lasound) требуют динамической линковки (без -static), иначе линковщик ищет отсутствующий libasound.a
- При выводе строковых констант через write() использовать strlen() вместо жестко заданных байтов во избежание -Wstringop-overread

## 9. Сессия аудита и самопроверки (/boost)
- `packages/atri-led` очищен от аудио/DSP: файлы `vqe_spotter_pipeline.c`, `yandex_spotter.h`, `yandex_vqe.h` перенесены в `packages/atri-audio/dsp`. В `atri-led` только LED ring.
- `README.md`: удалены все стикеры и эмодзи. В таблице статусов подсистем установлены галочки `[ ] Untested on hardware` до проверки на физическом железе.
- `packages/atri-display`: исправлена ошибка компиляции в `src/atri_display_cli.c` (добавлен недостающий `#include <errno.h>`).
- `packages/atri-audio`: в `Makefile` убран `-static` из `LDFLAGS` для корректной динамической линковки с `libasound.so`.
- `packages/atri-wireless`: устранены ворнинги `-Wstringop-overread` в `src/atri_onboard.c` через `send_resp(..., strlen(...))`; добавлен `#include <time.h>` в `src/atri_wireless_init.c`.
- `packages/atri-led/debian/prerm`: добавлены `atri-led-boot.service` и `atri-led-stop-boot.service` в секцию остановки и отключения при удалении пакета.
- Добавлены `.gitignore` во все 5 пакетов (`atri-led`, `atri-display`, `atri-audio`, `atri-wireless`, `atri-tools`).
- Все 5 пакетов проверены реальной сборкой в окружении WSL (`make clean all`) — успешный выход (exit code 0).
