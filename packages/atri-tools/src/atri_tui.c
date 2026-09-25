/*
 * atri_tui.c - Interactive Terminal UI Configurator for AtriOS
 *
 * Provides a raspi-config / nmtui style interactive menu:
 *   - Phone setup mode (SoftAP hotspot + mobile onboarding web portal)
 *   - Wi-Fi scanner and connection wizard
 *   - Audio diagnostic and hardware tests (tweeters, woofer, sweep, mics)
 *   - 25x16 LED matrix & 24-LED RGB ring control
 *   - Ambient light sensor (LTR-308) and auto-brightness regulation
 *   - Zigbee coordinator management
 *   - System status dashboard
 *
 * Copyright (c) 2026 leftymods / AtriOS Project
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>

#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_CYAN    "\033[1;36m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_BLUE    "\033[1;34m"
#define COLOR_MAGENTA "\033[1;35m"
#define COLOR_REVERSE "\033[7m"

static struct termios orig_termios;
static bool raw_mode = false;

static void disable_raw_mode(void)
{
	if (raw_mode) {
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
		printf("\033[?25h"); /* Show cursor */
		fflush(stdout);
		raw_mode = false;
	}
}

static void enable_raw_mode(void)
{
	tcgetattr(STDIN_FILENO, &orig_termios);
	struct termios raw = orig_termios;
	raw.c_lflag &= ~(ECHO | ICANON | ISIG);
	raw.c_iflag &= ~(IXON | ICRNL);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
	printf("\033[?25l"); /* Hide cursor */
	fflush(stdout);
	raw_mode = true;
}

static void sigint_handler(int sig)
{
	(void)sig;
	disable_raw_mode();
	printf("\n\nExiting AtriOS Configurator.\n");
	exit(0);
}

static int read_key(void)
{
	char c;
	if (read(STDIN_FILENO, &c, 1) != 1) return -1;
	if (c == '\033') {
		char seq[3];
		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\033';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\033';
		if (seq[0] == '[') {
			switch (seq[1]) {
				case 'A': return 1000; /* UP */
				case 'B': return 1001; /* DOWN */
				case 'C': return 1002; /* RIGHT */
				case 'D': return 1003; /* LEFT */
			}
		}
		return '\033';
	}
	return c;
}

static void clear_screen(void)
{
	printf("\033[2J\033[H");
	fflush(stdout);
}

static void wait_enter(void)
{
	disable_raw_mode();
	printf("\n%sНажмите [ENTER] для возврата в меню...%s", COLOR_BOLD, COLOR_RESET);
	fflush(stdout);
	getchar();
	enable_raw_mode();
}

/* Action Handlers */

static void action_phone_setup(void)
{
	disable_raw_mode();
	clear_screen();
	printf("%s=== AtriOS: Режим первоначальной настройки через Bluetooth ===%s\n\n", COLOR_CYAN, COLOR_RESET);
	printf("Выберите режим сопряжения:\n\n");
	printf("  %s1. Режим мобильного приложения (Скрытый BLE / App Mode) [По умолчанию]%s\n", COLOR_BOLD, COLOR_RESET);
	printf("     Станция не видна в обычном Bluetooth-поиске телефона.\n");
	printf("     Ее находит будущее приложение AtriOS по BLE Service UUID (0xFE33).\n\n");
	printf("  %s2. Тестовый режим (Видимый Classic Bluetooth / Test Mode)%s\n", COLOR_BOLD, COLOR_RESET);
	printf("     Станция объявляет себя как 'AtriStation-Setup-XXXX'.\n");
	printf("     Видна в стандартном поиске Bluetooth на телефоне для ручной проверки.\n\n");
	printf("  0. Назад\n\n");
	printf("Выберите режим [1]: ");

	char sel[16] = {0};
	if (fgets(sel, sizeof(sel), stdin)) {
		sel[strcspn(sel, "\r\n")] = '\0';
		if (sel[0] == '0') {
			enable_raw_mode();
			return;
		}
		if (sel[0] == '2') {
			printf("\nЗапуск в тестовом видимом режиме (Classic BT Piscan)...\n");
			system("atri-onboard --visible");
		} else {
			printf("\nЗапуск в скрытом режиме приложения (BLE UUID 0xFE33 + Classic Noscan)...\n");
			system("atri-onboard");
		}
	} else {
		system("atri-onboard");
	}

	wait_enter();
}

static void action_wifi(void)
{
	disable_raw_mode();
	clear_screen();
	printf("%s=== Сканирование сетей Wi-Fi ===%s\n\n", COLOR_CYAN, COLOR_RESET);
	system("nmcli dev wifi list || atri wifi");

	printf("\n%sПодключиться к сети?%s [y/N]: ", COLOR_BOLD, COLOR_RESET);
	char ans[16];
	if (fgets(ans, sizeof(ans), stdin) && (ans[0] == 'y' || ans[0] == 'Y' || ans[0] == 'д' || ans[0] == 'Д')) {
		char ssid[64] = {0};
		char pass[64] = {0};
		printf("Введите SSID сети: ");
		if (fgets(ssid, sizeof(ssid), stdin)) ssid[strcspn(ssid, "\r\n")] = '\0';
		printf("Введите пароль: ");
		if (fgets(pass, sizeof(pass), stdin)) pass[strcspn(pass, "\r\n")] = '\0';

		char cmd[256];
		if (pass[0]) {
			snprintf(cmd, sizeof(cmd), "nmcli dev wifi connect '%s' password '%s'", ssid, pass);
		} else {
			snprintf(cmd, sizeof(cmd), "nmcli dev wifi connect '%s'", ssid);
		}
		printf("\nПодключение...\n");
		system(cmd);
	}
	wait_enter();
}

static void action_sound(void)
{
	disable_raw_mode();
	clear_screen();
	printf("%s=== Аудио диагностика AtriStation ===%s\n", COLOR_CYAN, COLOR_RESET);
	printf("1. Проверка синусоидального тона (Stereo Tweeters 440 Hz)\n");
	printf("2. Проверка сабвуфера (Woofer 80 Hz Sub-bass)\n");
	printf("3. Частотный свип (40 Hz -> 10 kHz)\n");
	printf("4. Тест 4 микрофонов с живым VU-метром\n");
	printf("5. Полный аудит звуковой подсистемы (I2C, 20V, ALSA)\n");
	printf("6. Сброс громкости и размьют каналов (Unmute 75%%)\n");
	printf("0. Назад\n\n");
	printf("Выберите пункт [0-6]: ");

	char sel[16];
	if (fgets(sel, sizeof(sel), stdin)) {
		switch (sel[0]) {
			case '1': system("atri sound tone tweeters || atri-sound-test tone left"); break;
			case '2': system("atri sound tone sub || atri-sound-test tone sub"); break;
			case '3': system("atri sound sweep || atri-sound-test sweep"); break;
			case '4': system("atri sound mic 5 || atri-sound-test mic 5"); break;
			case '5': system("atri sound status || atri-sound-test status"); break;
			case '6': system("atri sound unmute || atri-sound-test unmute"); break;
			default: break;
		}
	}
	wait_enter();
}

static void action_display_led(void)
{
	disable_raw_mode();
	clear_screen();
	printf("%s=== Экран 25x16 и световое кольцо 24-LED ===%s\n", COLOR_CYAN, COLOR_RESET);
	printf("1. Запуск графического демо Pong на экране\n");
	printf("2. Вывести бегущий текст на экран\n");
	printf("3. Тест пикселей экрана (сетка, рамки, заливка)\n");
	printf("4. Запуск анимации «Радуга» на кольце\n");
	printf("5. Установить цвет кольца (Бирюзовый AtriOS)\n");
	printf("6. Выключить кольцо и экран\n");
	printf("0. Назад\n\n");
	printf("Выберите пункт [0-6]: ");

	char sel[16];
	if (fgets(sel, sizeof(sel), stdin)) {
		switch (sel[0]) {
			case '1': system("atri matrix demo bounce || atri-matrix demo bounce"); break;
			case '2': {
				char txt[64];
				printf("Введите текст: ");
				if (fgets(txt, sizeof(txt), stdin)) {
					txt[strcspn(txt, "\r\n")] = '\0';
					char cmd[128]; snprintf(cmd, sizeof(cmd), "atri matrix text '%s'", txt);
					system(cmd);
				}
				break;
			}
			case '3': system("atri matrix test || atri-matrix test"); break;
			case '4': system("atri led loop rainbow || atri-led-ctl loop rainbow"); break;
			case '5': system("atri led color 0 210 255 || atri-led-ctl color 0 210 255"); break;
			case '6':
				system("atri matrix off 2>/dev/null; atri led off 2>/dev/null");
				printf("Экран и кольцо выключены.\n");
				break;
			default: break;
		}
	}
	wait_enter();
}

static void action_als(void)
{
	disable_raw_mode();
	clear_screen();
	printf("%s=== Автояркость и датчик освещенности LTR-308 ===%s\n", COLOR_CYAN, COLOR_RESET);
	printf("1. Текущий статус автояркости\n");
	printf("2. Мониторинг люксов в реальном времени (Watch)\n");
	printf("3. Включить службу автояркости\n");
	printf("4. Выключить службу автояркости\n");
	printf("5. Включить автовыключение в темноте (< 2 люкс)\n");
	printf("6. Отключить автовыключение в темноте\n");
	printf("0. Назад\n\n");
	printf("Выберите пункт [0-6]: ");

	char sel[16];
	if (fgets(sel, sizeof(sel), stdin)) {
		switch (sel[0]) {
			case '1': system("atri autobrightness status"); break;
			case '2':
				printf("Нажмите Ctrl+C для выхода...\n");
				system("atri als watch");
				break;
			case '3': system("atri autobrightness on"); break;
			case '4': system("atri autobrightness off"); break;
			case '5': system("atri autobrightness auto-off on"); break;
			case '6': system("atri autobrightness auto-off off"); break;
			default: break;
		}
	}
	wait_enter();
}

static void action_btaudio(void)
{
	disable_raw_mode();
	clear_screen();
	printf("%s=== Bluetooth-колонка (A2DP Audio Sink) ===%s\n\n", COLOR_CYAN, COLOR_RESET);
	printf("1. Включить режим беспроводной колонки (видимый для смартфона)\n");
	printf("2. Выключить видимость Bluetooth\n");
	printf("3. Список сопряженных и подключенных устройств\n");
	printf("0. Назад\n\n");
	printf("Выберите пункт [0-3]: ");

	char sel[16];
	if (fgets(sel, sizeof(sel), stdin)) {
		switch (sel[0]) {
			case '1':
				system("hciconfig hci0 up 2>/dev/null || true; "
				       "hciconfig hci0 piscan 2>/dev/null || true; "
				       "hciconfig hci0 class 0x200414 2>/dev/null || true; "
				       "bluetoothctl discoverable on >/dev/null 2>&1 & "
				       "bluetoothctl pairable on >/dev/null 2>&1 &");
				printf("\n%s[OK]%s Режим Bluetooth-колонки включен. Станция видна на смартфоне как аудиоустройство.\n", COLOR_GREEN, COLOR_RESET);
				break;
			case '2':
				system("hciconfig hci0 noscan 2>/dev/null || true; "
				       "bluetoothctl discoverable off >/dev/null 2>&1 &");
				printf("\n%s[OK]%s Видимость Bluetooth отключена.\n", COLOR_GREEN, COLOR_RESET);
				break;
			case '3':
				printf("\nПодключенные устройства:\n");
				system("bluetoothctl devices Connected 2>/dev/null || bluetoothctl devices");
				break;
			default: break;
		}
	}
	wait_enter();
}

static void action_locale(void)
{
	disable_raw_mode();
	clear_screen();
	printf("%s=== Язык и региональные настройки системы ===%s\n\n", COLOR_CYAN, COLOR_RESET);
	printf("1. Русский (ru_RU.UTF-8)\n");
	printf("2. English (en_US.UTF-8)\n");
	printf("3. Текущий статус локали системы\n");
	printf("0. Назад\n\n");
	printf("Выберите пункт [0-3]: ");

	char sel[16];
	if (fgets(sel, sizeof(sel), stdin)) {
		switch (sel[0]) {
			case '1':
				printf("Установка русской локали...\n");
				system("localectl set-locale LANG=ru_RU.UTF-8 2>/dev/null || (echo 'LANG=ru_RU.UTF-8' > /etc/default/locale)");
				printf("%s[OK]%s Установлен язык: Русский (ru_RU.UTF-8)\n", COLOR_GREEN, COLOR_RESET);
				break;
			case '2':
				printf("Setting English locale...\n");
				system("localectl set-locale LANG=en_US.UTF-8 2>/dev/null || (echo 'LANG=en_US.UTF-8' > /etc/default/locale)");
				printf("%s[OK]%s Language set to: English (en_US.UTF-8)\n", COLOR_GREEN, COLOR_RESET);
				break;
			case '3':
				system("localectl status 2>/dev/null || cat /etc/default/locale 2>/dev/null");
				break;
			default: break;
		}
	}
	wait_enter();
}

static void action_timezone(void)
{
	disable_raw_mode();
	clear_screen();
	printf("%s=== Настройка часового пояса и синхронизации времени ===%s\n\n", COLOR_CYAN, COLOR_RESET);
	printf("1. Москва, Санкт-Петербург (UTC+3, Europe/Moscow)\n");
	printf("2. Калининград (UTC+2, Europe/Kaliningrad)\n");
	printf("3. Самара (UTC+4, Europe/Samara)\n");
	printf("4. Екатеринбург (UTC+5, Asia/Yekaterinburg)\n");
	printf("5. Омск (UTC+6, Asia/Omsk)\n");
	printf("6. Новосибирск, Красноярск (UTC+7, Asia/Novosibirsk)\n");
	printf("7. Иркутск (UTC+8, Asia/Irkutsk)\n");
	printf("8. Владивосток (UTC+10, Asia/Vladivostok)\n");
	printf("9. Всемирное время UTC (Etc/UTC)\n");
	printf("N. Включить синхронизацию времени по NTP (systemd-timesyncd)\n");
	printf("0. Назад\n\n");
	printf("Выберите пункт: ");

	char sel[16];
	if (fgets(sel, sizeof(sel), stdin)) {
		const char *tz = NULL;
		switch (sel[0]) {
			case '1': tz = "Europe/Moscow"; break;
			case '2': tz = "Europe/Kaliningrad"; break;
			case '3': tz = "Europe/Samara"; break;
			case '4': tz = "Asia/Yekaterinburg"; break;
			case '5': tz = "Asia/Omsk"; break;
			case '6': tz = "Asia/Novosibirsk"; break;
			case '7': tz = "Asia/Irkutsk"; break;
			case '8': tz = "Asia/Vladivostok"; break;
			case '9': tz = "Etc/UTC"; break;
			case 'n': case 'N':
				system("timedatectl set-ntp true 2>/dev/null || true");
				printf("%s[OK]%s Синхронизация времени по NTP активирована.\n", COLOR_GREEN, COLOR_RESET);
				break;
			default: break;
		}
		if (tz) {
			char cmd[128];
			snprintf(cmd, sizeof(cmd), "timedatectl set-timezone '%s' 2>/dev/null || (echo '%s' > /etc/timezone)", tz, tz);
			system(cmd);
			printf("%s[OK]%s Часовой пояс установлен: %s\n", COLOR_GREEN, COLOR_RESET, tz);
			system("date");
		}
	}
	wait_enter();
}

static void action_display_daemon(void)
{
	disable_raw_mode();
	clear_screen();
	printf("%s=== Управление дисплеем 25x16 (atri-display) ===%s\n\n", COLOR_CYAN, COLOR_RESET);
	printf("1. Режим цифровых часов (Digital Clock HH:MM)\n");
	printf("2. Анимация глазок (Blinking Eyes)\n");
	printf("3. Вывод текущей температуры (+22)\n");
	printf("4. Вывести IP-адрес станции бегущей строкой\n");
	printf("5. Очистить экран\n");
	printf("6. Статус службы экрана\n");
	printf("0. Назад\n\n");
	printf("Выберите пункт [0-6]: ");

	char sel[16];
	if (fgets(sel, sizeof(sel), stdin)) {
		switch (sel[0]) {
			case '1': system("atri display clock || atri-display clock"); break;
			case '2': system("atri display eyes || atri-display eyes"); break;
			case '3': system("atri display temp +22 || atri-display temp +22"); break;
			case '4': system("atri display ip || atri-display ip"); break;
			case '5': system("atri display clear || atri-display clear"); break;
			case '6': system("atri display status || atri-display status"); break;
			default: break;
		}
	}
	wait_enter();
}

static void action_volume_control(void)
{
	int vol = 75;
	FILE *fp = popen("amixer sget Master 2>/dev/null | grep -m1 -o '[0-9]*%' | tr -d '%'", "r");
	if (fp) {
		int v; if (fscanf(fp, "%d", &v) == 1) vol = v;
		pclose(fp);
	}

	for (;;) {
		clear_screen();
		printf("%s=== Управление громкостью AtriStation ===%s\n\n", COLOR_CYAN, COLOR_RESET);
		printf("Текущая громкость: %s%3d%%%s\n\n", COLOR_BOLD, vol, COLOR_RESET);

		printf("Уровень: [");
		int bars = vol / 5;
		for (int b = 0; b < 20; b++) {
			if (b < bars) printf("%s█%s", COLOR_GREEN, COLOR_RESET);
			else printf("░");
		}
		printf("]\n\n");

		printf("Клавиши: [%s+%s/%s→%s] +5%%, [%s-%s/%s←%s] -5%%, [%st%s] Тест-тон, [%sq/ENTER%s] Выход\n",
		       COLOR_BOLD, COLOR_RESET, COLOR_BOLD, COLOR_RESET,
		       COLOR_BOLD, COLOR_RESET, COLOR_BOLD, COLOR_RESET,
		       COLOR_BOLD, COLOR_RESET, COLOR_BOLD, COLOR_RESET);

		int k = read_key();
		if (k == '+' || k == '=' || k == 1002 || k == 1000) {
			vol += 5;
			if (vol > 100) vol = 100;
			char cmd[128];
			snprintf(cmd, sizeof(cmd), "atrivolume %d 2>/dev/null || amixer sset Master %d%% >/dev/null 2>&1", vol, vol);
			system(cmd);
		} else if (k == '-' || k == '_' || k == 1003 || k == 1001) {
			vol -= 5;
			if (vol < 0) vol = 0;
			char cmd[128];
			snprintf(cmd, sizeof(cmd), "atrivolume %d 2>/dev/null || amixer sset Master %d%% >/dev/null 2>&1", vol, vol);
			system(cmd);
		} else if (k == 't' || k == 'T') {
			system("atri sound tone tweeters >/dev/null 2>&1 &");
		} else if (k == 'q' || k == 'Q' || k == '\r' || k == '\n' || k == 27) {
			break;
		}
	}
}

static void action_zigbee(void)
{
	disable_raw_mode();
	clear_screen();
	printf("%s=== Управление Zigbee координатором (Tuya TZ9213) ===%s\n", COLOR_CYAN, COLOR_RESET);
	printf("1. Запуск прослушивания эфира (Sniff / Listen)\n");
	printf("2. Информация о радиомодуле и прошивке\n");
	printf("3. Аппаратный сброс модуля (Reset GPIOX_17)\n");
	printf("0. Назад\n\n");
	printf("Выберите пункт [0-3]: ");

	char sel[16];
	if (fgets(sel, sizeof(sel), stdin)) {
		switch (sel[0]) {
			case '1':
				printf("Слушаю эфир Zigbee (/dev/ttyAML2). Ctrl+C для выхода...\n");
				system("atri zigbee listen || atri-zigbee listen");
				break;
			case '2': system("atri zigbee info || atri-zigbee info"); break;
			case '3': system("atri zigbee reset || atri-zigbee reset"); break;
			default: break;
		}
	}
	wait_enter();
}

static void action_status(void)
{
	disable_raw_mode();
	clear_screen();
	system("atri status");
	wait_enter();
}

static void action_power(void)
{
	disable_raw_mode();
	clear_screen();
	printf("%s=== Управление питанием станции ===%s\n", COLOR_CYAN, COLOR_RESET);
	printf("1. Перезагрузка (Reboot)\n");
	printf("2. Выключение (Power Off)\n");
	printf("0. Отмена\n\n");
	printf("Выберите действие [0-2]: ");

	char sel[16];
	if (fgets(sel, sizeof(sel), stdin)) {
		if (sel[0] == '1') {
			printf("Перезагрузка системы...\n");
			system("reboot");
			exit(0);
		} else if (sel[0] == '2') {
			printf("Выключение системы...\n");
			system("poweroff");
			exit(0);
		}
	}
	enable_raw_mode();
}

struct menu_item {
	const char *title;
	const char *desc;
	void (*func)(void);
};

static struct menu_item menu[] = {
	{ "📱 Настройка через Bluetooth (Phone Onboarding)", "Сопряжение со смартфоном: передача Wi-Fi, языка, зоны и имени", action_phone_setup },
	{ "🎵 Bluetooth-колонка (A2DP Audio Sink)", "Воспроизведение музыки со смартфона на динамиках станции", action_btaudio },
	{ "📶 Настройка Wi-Fi (Wi-Fi Networks)", "Сканирование домашних сетей и подключение через NetworkManager", action_wifi },
	{ "🔊 Громкость (Interactive Volume Slider)", "Интерактивный регулятор громкости и тест динамиков", action_volume_control },
	{ "🔊 Тест звука (Audio Hardware & Tests)", "Тест твитеров, вуфера, частотный свип и 4-ch микрофоны", action_sound },
	{ "🖥️ Режимы экрана 25x16 (Clock, Eyes, Temp)", "Цифровые часы, анимация глаз, температура, IP-адрес", action_display_daemon },
	{ "💡 Экран 25x16 и световое кольцо (LEDs)", "Анимации, бегущий текст, Pong demo, цвета и подсветка", action_display_led },
	{ "☀️ Автояркость по ALS (Auto-Brightness)", "Датчик LTR-308, плавная калибровка и автовыключение в темноте", action_als },
	{ "🌐 Язык и локализация (System Locale)", "Выбор русского (ru_RU) или английского (en_US) языка", action_locale },
	{ "🕒 Часовой пояс и время (Timezone & NTP)", "Выбор часового пояса и синхронизация времени по NTP", action_timezone },
	{ "🐝 Zigbee координатор (Smart Home Radio)", "Управление радиомодулем Tuya TZ9213 умного дома", action_zigbee },
	{ "📊 Сводный статус станции (System Health)", "Детальный аудит процессора, памяти, звука и радиомодулей", action_status },
	{ "🔄 Питание (Reboot / Power Off)", "Перезагрузка или безопасное отключение станции", action_power },
	{ "🚪 Выход в консоль (Exit)", "Завершить работу конфигуратора", NULL }
};

int main(int argc, char **argv)
{
	(void)argc; (void)argv;

	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);

	enable_raw_mode();

	int total_items = sizeof(menu) / sizeof(menu[0]);
	int selected = 0;

	for (;;) {
		clear_screen();
		printf("%s", COLOR_CYAN);
		printf("┌────────────────────────────────────────────────────────┐\n");
		printf("│           %sAtriOS Configuration Utility (TUI)%s%s         │\n", COLOR_BOLD, COLOR_RESET, COLOR_CYAN);
		printf("│    Автономная платформа умной колонки AtriStation     │\n");
		printf("└────────────────────────────────────────────────────────┘\n%s", COLOR_RESET);
		printf("Используйте стрелки [%s↑%s/%s↓%s], [%sENTER%s] выбор, [%sq%s] выход\n\n",
		       COLOR_BOLD, COLOR_RESET, COLOR_BOLD, COLOR_RESET, COLOR_BOLD, COLOR_RESET, COLOR_BOLD, COLOR_RESET);

		for (int i = 0; i < total_items; i++) {
			if (i == selected) {
				printf(" %s %s%-46s%s\n", COLOR_CYAN, COLOR_REVERSE, menu[i].title, COLOR_RESET);
				printf("   %s%s↳ %s%s\n", COLOR_BOLD, COLOR_YELLOW, menu[i].desc, COLOR_RESET);
			} else {
				printf("   %-46s\n", menu[i].title);
			}
		}
		printf("\n");

		int k = read_key();
		if (k == 1000) { /* UP */
			selected = (selected - 1 + total_items) % total_items;
		} else if (k == 1001) { /* DOWN */
			selected = (selected + 1) % total_items;
		} else if (k == '\r' || k == '\n' || k == 10) { /* ENTER */
			if (menu[selected].func) {
				menu[selected].func();
				enable_raw_mode();
			} else {
				break;
			}
		} else if (k == 'q' || k == 'Q' || k == 27) { /* ESC or q */
			break;
		}
	}

	disable_raw_mode();
	clear_screen();
	printf("AtriOS Configurator завершил работу.\n");
	return 0;
}
