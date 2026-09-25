/*
 * atri_sound_test.c - Comprehensive audio hardware diagnostic and test tool for AtriOS
 *
 * Hardware target:
 *   - Amplifiers: SY6045S Tweeters (@0x2a) + SY6045S Woofer (@0x2b)
 *   - DAC: ES8156 (@0x08)
 *   - ADC: ES7210 / DMIC microphone array (@0x40)
 *   - Power supply: 20V_AMPL regulator
 *
 * Features:
 *   - Hardware probe: I2C codecs, 20V amplifier power rail, ALSA PCM endpoints
 *   - Generator: Stereo sine test tones (left tweeter, right tweeter, woofer bass)
 *   - Frequency sweep (40 Hz -> 10 kHz)
 *   - Microphone array recorder & live ASCII VU-meter
 *
 * Copyright (c) 2026 leftymods / AtriOS Project
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <alsa/asoundlib.h>

#define PI 3.14159265358979323846
#define SAMPLE_RATE 48000
#define CHANNELS 2

static volatile bool running = true;
static void sigint_handler(int sig) { (void)sig; running = false; }

/* --- Hardware Audit --- */

static bool check_i2c_addr(uint8_t addr)
{
	const char *buses[] = { "/dev/i2c-0", "/dev/i2c-1", "/dev/i2c-2", NULL };
	for (int i = 0; buses[i]; i++) {
		int fd = open(buses[i], O_RDWR);
		if (fd >= 0) {
			if (ioctl(fd, I2C_SLAVE_FORCE, addr) >= 0) {
				uint8_t dummy = 0;
				if (write(fd, &dummy, 0) == 0) {
					close(fd);
					return true;
				}
			}
			close(fd);
		}
	}
	return false;
}

static bool check_20v_supply(void)
{
	DIR *d = opendir("/sys/class/regulator");
	if (!d) return false;
	struct dirent *de;
	char path[256], buf[64];
	bool ok = false;

	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "regulator.", 10) != 0) continue;
		snprintf(path, sizeof(path), "/sys/class/regulator/%s/name", de->d_name);
		int fd = open(path, O_RDONLY);
		if (fd >= 0) {
			ssize_t n = read(fd, buf, sizeof(buf) - 1);
			close(fd);
			if (n > 0) {
				buf[n] = '\0';
				if (strstr(buf, "20V_AMPL") || strstr(buf, "ampl_pwr")) {
					ok = true;
					break;
				}
			}
		}
	}
	closedir(d);
	return ok;
}

static void audit_sound_hardware(void)
{
	printf("====================================================\n");
	printf("       AtriOS Audio Hardware Audit (SY6045S/ES8156) \n");
	printf("====================================================\n\n");

	/* 1. Check ALSA cards */
	printf("[1] ALSA Sound Cards (/proc/asound/cards):\n");
	FILE *f = fopen("/proc/asound/cards", "r");
	if (f) {
		char line[256];
		bool found_atri = false;
		while (fgets(line, sizeof(line), f)) {
			printf("    %s", line);
			if (strstr(line, "ATRISTATION")) found_atri = true;
		}
		fclose(f);
		if (found_atri) {
			printf("    ==> ATRISTATION sound card detected [OK]\n");
		} else {
			printf("    ==> WARNING: ATRISTATION card not found in /proc/asound/cards\n");
		}
	} else {
		printf("    ==> Failed to open /proc/asound/cards\n");
	}
	printf("\n");

	/* 2. Check PCM endpoints */
	printf("[2] PCM Stream Endpoints:\n");
	printf("    Playback (/dev/snd/pcmC0D0p) : %s\n",
	       access("/dev/snd/pcmC0D0p", F_OK) == 0 ? "AVAILABLE [OK]" : "MISSING [FAIL]");
	printf("    Capture  (/dev/snd/pcmC0D0c) : %s\n",
	       access("/dev/snd/pcmC0D0c", F_OK) == 0 ? "AVAILABLE [OK]" : "MISSING [INFO]");
	printf("    PDM Mics (/dev/snd/pcmC0D1c) : %s\n",
	       access("/dev/snd/pcmC0D1c", F_OK) == 0 ? "AVAILABLE [OK]" : "MISSING [INFO]");
	printf("\n");

	/* 3. Check 20V power regulator */
	printf("[3] Amplifier Power Supply:\n");
	bool pwr20v = check_20v_supply();
	printf("    20V_AMPL regulator           : %s\n",
	       pwr20v ? "REGISTERED [OK]" : "NOT REGISTERED (or fixed rail) [INFO]");
	printf("\n");

	/* 4. Check I2C Audio Codecs and Amplifiers */
	printf("[4] I2C Audio Codecs & Amplifiers:\n");
	bool tw_ok = check_i2c_addr(0x2a);
	bool wf_ok = check_i2c_addr(0x2b);
	bool dac_ok = check_i2c_addr(0x08);
	bool adc_ok = check_i2c_addr(0x40);

	printf("    SY6045S Tweeters Amp (0x2a)  : %s\n", tw_ok ? "ACK [OK]" : "NO RESPONSE [FAIL]");
	printf("    SY6045S Woofer Amp   (0x2b)  : %s\n", wf_ok ? "ACK [OK]" : "NO RESPONSE [FAIL]");
	printf("    ES8156 Audio DAC     (0x08)  : %s\n", dac_ok ? "ACK [OK]" : "NO RESPONSE [FAIL]");
	printf("    ES7210 Feedback ADC  (0x40)  : %s\n", adc_ok ? "ACK [OK]" : "NO RESPONSE [FAIL]");
	printf("\n");

	/* 5. Kernel Modules Check */
	printf("[5] Audio Kernel Modules (/sys/module):\n");
	printf("    snd_soc_sy6045s              : %s\n",
	       access("/sys/module/snd_soc_sy6045s", F_OK) == 0 ? "LOADED [OK]" : "NOT LOADED");
	printf("    snd_soc_es8156               : %s\n",
	       access("/sys/module/snd_soc_es8156", F_OK) == 0 ? "LOADED [OK]" : "NOT LOADED");
	printf("    snd_soc_es7210               : %s\n",
	       access("/sys/module/snd_soc_es7210", F_OK) == 0 ? "LOADED [OK]" : "NOT LOADED");
	printf("====================================================\n\n");
}

/* --- Tone Generator using ALSA --- */

static int play_tone(double freq_l, double freq_r, double duration_sec, double amplitude)
{
	snd_pcm_t *handle;
	int err;

	const char *device = "default";
	if ((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		device = "hw:ATRISTATION,0";
		if ((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
			fprintf(stderr, "Playback open error: %s (%s)\n", snd_strerror(err), device);
			return -1;
		}
	}

	if ((err = snd_pcm_set_params(handle,
				      SND_PCM_FORMAT_S16_LE,
				      SND_PCM_ACCESS_RW_INTERLEAVED,
				      CHANNELS,
				      SAMPLE_RATE,
				      1,
				      500000)) < 0) {
		fprintf(stderr, "Playback set_params error: %s\n", snd_strerror(err));
		snd_pcm_close(handle);
		return -1;
	}

	int total_frames = (int)(duration_sec * SAMPLE_RATE);
	int period_frames = 1024;
	int16_t *buf = malloc(period_frames * CHANNELS * sizeof(int16_t));
	if (!buf) {
		snd_pcm_close(handle);
		return -1;
	}

	int frames_left = total_frames;
	int frame_idx = 0;

	while (running && frames_left > 0) {
		int chunk = (frames_left > period_frames) ? period_frames : frames_left;
		for (int i = 0; i < chunk; i++) {
			double t = (double)(frame_idx + i) / SAMPLE_RATE;
			double s_l = (freq_l > 0) ? sin(2.0 * PI * freq_l * t) : 0.0;
			double s_r = (freq_r > 0) ? sin(2.0 * PI * freq_r * t) : 0.0;

			buf[i * 2 + 0] = (int16_t)(s_l * amplitude * 32767.0);
			buf[i * 2 + 1] = (int16_t)(s_r * amplitude * 32767.0);
		}

		snd_pcm_sframes_t written = snd_pcm_writei(handle, buf, chunk);
		if (written < 0) {
			written = snd_pcm_recover(handle, written, 0);
		}
		if (written < 0) {
			fprintf(stderr, "snd_pcm_writei failed: %s\n", snd_strerror(written));
			break;
		}
		frame_idx += written;
		frames_left -= written;
	}

	snd_pcm_drain(handle);
	snd_pcm_close(handle);
	free(buf);
	return 0;
}

static int play_sweep(double start_freq, double end_freq, double duration_sec)
{
	snd_pcm_t *handle;
	int err;

	if ((err = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		if ((err = snd_pcm_open(&handle, "hw:ATRISTATION,0", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
			fprintf(stderr, "Playback open error: %s\n", snd_strerror(err));
			return -1;
		}
	}

	snd_pcm_set_params(handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
			   CHANNELS, SAMPLE_RATE, 1, 500000);

	int total_frames = (int)(duration_sec * SAMPLE_RATE);
	int period_frames = 1024;
	int16_t *buf = malloc(period_frames * CHANNELS * sizeof(int16_t));
	if (!buf) { snd_pcm_close(handle); return -1; }

	int frames_left = total_frames;
	int frame_idx = 0;
	double phase = 0.0;

	while (running && frames_left > 0) {
		int chunk = (frames_left > period_frames) ? period_frames : frames_left;
		for (int i = 0; i < chunk; i++) {
			double progress = (double)(frame_idx + i) / total_frames;
			double freq = start_freq * pow(end_freq / start_freq, progress);
			phase += 2.0 * PI * freq / SAMPLE_RATE;
			if (phase >= 2.0 * PI) phase -= 2.0 * PI;

			int16_t val = (int16_t)(sin(phase) * 0.7 * 32767.0);
			buf[i * 2 + 0] = val;
			buf[i * 2 + 1] = val;
		}

		snd_pcm_sframes_t written = snd_pcm_writei(handle, buf, chunk);
		if (written < 0) written = snd_pcm_recover(handle, written, 0);
		if (written < 0) break;
		frame_idx += written;
		frames_left -= written;
	}

	snd_pcm_drain(handle);
	snd_pcm_close(handle);
	free(buf);
	return 0;
}

/* --- Microphone VU Meter --- */

static void render_vu_bar(const char *name, double level)
{
	int bar_len = (int)(level * 30.0);
	if (bar_len > 30) bar_len = 30;
	printf("%s [", name);
	for (int i = 0; i < 30; i++) {
		if (i < bar_len) printf("#");
		else printf(" ");
	}
	printf("] %3.0f%%\n", level * 100.0);
}

static int run_mic_vu(int duration_sec)
{
	snd_pcm_t *capture_handle;
	int err;
	const char *cap_dev = "hw:ATRISTATION,0";

	if ((err = snd_pcm_open(&capture_handle, cap_dev, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
		cap_dev = "default";
		if ((err = snd_pcm_open(&capture_handle, cap_dev, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
			fprintf(stderr, "Cannot open capture device: %s\n", snd_strerror(err));
			return -1;
		}
	}

	unsigned int rate = 48000;
	int channels = 4; /* 4-channel microphone array */
	if ((err = snd_pcm_set_params(capture_handle, SND_PCM_FORMAT_S16_LE,
				      SND_PCM_ACCESS_RW_INTERLEAVED,
				      channels, rate, 1, 100000)) < 0) {
		/* Fallback to stereo if 4-ch not directly supported */
		channels = 2;
		snd_pcm_set_params(capture_handle, SND_PCM_FORMAT_S16_LE,
				   SND_PCM_ACCESS_RW_INTERLEAVED,
				   channels, rate, 1, 100000);
	}

	int period = 2048;
	int16_t *cbuf = malloc(period * channels * sizeof(int16_t));
	if (!cbuf) { snd_pcm_close(capture_handle); return -1; }

	printf("Listening on microphone (%d channels) for %d seconds. Make sound! Ctrl+C to stop.\n\n",
	       channels, duration_sec);

	time_t start = time(NULL);
	while (running && (time(NULL) - start < duration_sec)) {
		snd_pcm_sframes_t r = snd_pcm_readi(capture_handle, cbuf, period);
		if (r < 0) {
			r = snd_pcm_recover(capture_handle, r, 0);
			if (r < 0) continue;
		}

		/* Calculate peak/RMS per channel */
		double max_lvl[4] = {0};
		for (int i = 0; i < r; i++) {
			for (int ch = 0; ch < channels && ch < 4; ch++) {
				double v = fabs((double)cbuf[i * channels + ch]) / 32768.0;
				if (v > max_lvl[ch]) max_lvl[ch] = v;
			}
		}

		/* Print live VU meter (ANSI clear 4 lines up) */
		printf("\033[%dA", channels);
		for (int ch = 0; ch < channels && ch < 4; ch++) {
			char name[16]; snprintf(name, sizeof(name), "MIC%d", ch + 1);
			render_vu_bar(name, max_lvl[ch]);
		}
		usleep(60000);
	}

	snd_pcm_close(capture_handle);
	free(cbuf);
	printf("\nMicrophone test finished.\n");
	return 0;
}

static void print_usage(const char *prog)
{
	printf("Usage: %s <command> [args]\n\n", prog);
	printf("Commands:\n");
	printf("  status / audit           Perform comprehensive hardware and driver check\n");
	printf("  tone [all|left|right|sub] Play sine tone (left/right tweeters @ 440Hz, sub @ 80Hz)\n");
	printf("  sweep                    Play frequency sweep from 40 Hz to 10 kHz\n");
	printf("  mic [seconds]            Live VU-meter for microphone capture (default 10s)\n");
	printf("  unmute                   Force unmute all channels and set volume to 75%%\n");
}

int main(int argc, char *argv[])
{
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);

	if (argc < 2 || strcmp(argv[1], "status") == 0 || strcmp(argv[1], "audit") == 0) {
		audit_sound_hardware();
		return 0;
	}

	const char *cmd = argv[1];

	if (strcmp(cmd, "unmute") == 0) {
		printf("Unmuting all channels and setting volume to 75%%...\n");
		system("amixer -c ATRISTATION sset 'Tweeters Master' 75% unmute 2>/dev/null || true");
		system("amixer -c ATRISTATION sset 'Woofer Master' 75% unmute 2>/dev/null || true");
		system("amixer -c ATRISTATION sset 'Playback' 80% unmute 2>/dev/null || true");
		printf("Done.\n");
		return 0;
	}

	if (strcmp(cmd, "tone") == 0) {
		const char *target = (argc >= 3) ? argv[2] : "all";
		if (strcmp(target, "left") == 0) {
			printf("Playing 440 Hz tone on LEFT TWEETER for 2.5s...\n");
			play_tone(440.0, 0.0, 2.5, 0.7);
		} else if (strcmp(target, "right") == 0) {
			printf("Playing 440 Hz tone on RIGHT TWEETER for 2.5s...\n");
			play_tone(0.0, 440.0, 2.5, 0.7);
		} else if (strcmp(target, "sub") == 0 || strcmp(target, "woofer") == 0) {
			printf("Playing 80 Hz bass tone on WOOFER for 3.0s...\n");
			play_tone(80.0, 80.0, 3.0, 0.8);
		} else {
			printf("Playing 440 Hz tone on both TWEETERS & WOOFER for 2.5s...\n");
			play_tone(440.0, 440.0, 2.5, 0.7);
		}
		return 0;
	}

	if (strcmp(cmd, "sweep") == 0) {
		printf("Playing frequency sweep (40 Hz -> 10 kHz, 4 seconds)...\n");
		play_sweep(40.0, 10000.0, 4.0);
		return 0;
	}

	if (strcmp(cmd, "mic") == 0) {
		int sec = (argc >= 3) ? atoi(argv[2]) : 10;
		if (sec <= 0) sec = 10;
		return run_mic_vu(sec);
	}

	print_usage(argv[0]);
	return 1;
}
