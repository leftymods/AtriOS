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
	printf("%s=== AtriOS: Режим настройки с телефона ===%s\n\n", COLOR_CYAN, COLOR_RESET);
	printf("Станция запускает точку доступа Wi-Fi и веб-сервер первичной настройки...\n");
	printf("Подключите ваш смартфон или ноутбук к созданной сети:\n");
	printf("  %sSSID:%s      AtriOS-Setup-XXXX\n", COLOR_BOLD, COLOR_RESET);
	printf("  %sПароль:%s   atriossetup\n", COLOR_BOLD, COLOR_RESET);
	printf("  %sВеб-адрес:%s http://192.168.4.1:8080/ (или http://atri.local:8080/)\n\n", COLOR_GREEN, COLOR_RESET);
	printf("Через веб-страницу можно выбрать домашнюю сеть Wi-Fi, ввести пароль,\n");
	printf("задать имя колонки и протестировать звук.\n\n");
	printf("Запуск сервиса настройки (Ctrl+C для завершения)...\n\n");

	system("python3 /usr/libexec/atri_onboard.py || python3 packages/atri-led/src/atri_onboard.py");

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
	{ "📱 Настройка с телефона (Phone Setup Mode)", "Запуск SoftAP Wi-Fi и мобильного веб-портала настройки", action_phone_setup },
	{ "📶 Настройка Wi-Fi (Wi-Fi Networks)", "Сканирование домашних сетей и подключение через NetworkManager", action_wifi },
	{ "🔊 Тест звука (Audio Hardware & Tests)", "Тест твитеров, вуфера, частотный свип и 4-ch микрофоны", action_sound },
	{ "💡 Экран 25x16 и световое кольцо (LEDs)", "Анимации, бегущий текст, Pong demo, цвета и подсветка", action_display_led },
	{ "☀️ Автояркость по ALS (Auto-Brightness)", "Датчик LTR-308, плавная калибровка и автовыключение в темноте", action_als },
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
