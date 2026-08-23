/*
 * atri-screen-test.c — verbose test suite for the AtriStation LED panel
 * (kernel driver: gowin_led_device, fbdev "atri_led_panel_*").
 *
 * Checks:
 *   1. framebuffer discovery (by fix.id prefix) with per-candidate reasons
 *   2. fix/var sanity vs expectations (25x16 or 28x16, 8bpp)
 *   3. sysfs introspection of the SPI device (fw_upd_status, CRC counters,
 *      debug info) — shows whether the FPGA bitstream programmed OK
 *   4. pattern tests through the real push path (fsync -> fb_sync -> SPI):
 *      gray ramp, RGB solid, checkerboard, border, moving block
 *   5. backlight brightness sweep
 *
 * Every step is logged; errors carry errno strings. Exit code = failures.
 *
 * Build: cc -O2 -Wall -o atri-screen-test atri_screen_test.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

#define FB_ID_PREFIX	"atri_led_panel_"
#define EXP_W1		25
#define EXP_H1		16
#define EXP_W2		28

static int failures = 0;
static struct timespec t_prog_start;

/* ---------- logging ---------- */

static const char *tstamp(void)
{
	static char buf[32];
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	long ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
	snprintf(buf, sizeof(buf), "[%7ld.%03ld]", ms / 1000, ms % 1000);
	return buf;
}

#define LOG(fmt, ...) \
	fprintf(stdout, "%s " fmt "\n", tstamp(), ##__VA_ARGS__)
#define ERR(fmt, ...) \
	do { \
		int _e = errno; \
		failures++; \
		if (_e) \
			fprintf(stderr, "%s ERROR: " fmt " (%s)\n", tstamp(), \
				##__VA_ARGS__, strerror(_e)); \
		else \
			fprintf(stderr, "%s ERROR: " fmt "\n", tstamp(), \
				##__VA_ARGS__); \
	} while (0)
#define PASS(...) do { fprintf(stdout, "%s PASS: ", tstamp()); \
	fprintf(stdout, __VA_ARGS__); fputc('\n', stdout); } while (0)
#define FAIL(name, fmt, ...) \
	do { failures++; \
	     fprintf(stderr, "%s FAIL: %s — " fmt "\n", \
		     tstamp(), name, ##__VA_ARGS__); } while (0)

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ---------- sysfs introspection ---------- */

static void dump_sysfs_attr(const char *base, const char *name)
{
	char path[512];
	char buf[512];
	int fd, n;

	snprintf(path, sizeof(path), "%s/%s", base, name);
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		LOG("  sysfs %-22s : unavailable (%s)", name, strerror(errno));
		return;
	}
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n < 0) {
		LOG("  sysfs %-22s : read error (%s)", name, strerror(errno));
		return;
	}
	buf[n > 0 ? n : 0] = '\0';
	while (n > 0 && (buf[n-1] == '\n')) buf[--n] = '\0';
	LOG("  sysfs %-22s : %s", name, buf);
}

static int find_spi_device_sysfs(char *out, size_t outlen) /* out >= 512 */
{
	DIR *d = opendir("/sys/bus/spi/devices");
	struct dirent *de;
	char path[512], modalias[128];
	int fd, n;

	if (!d) {
		ERR("cannot open /sys/bus/spi/devices");
		return -1;
	}
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path),
			 "/sys/bus/spi/devices/%s/modalias", de->d_name);
		fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;
		n = read(fd, modalias, sizeof(modalias) - 1);
		close(fd);
		if (n <= 0)
			continue;
		modalias[n] = '\0';
		if (strstr(modalias, "spi:atri,led-panel") ||
		    strstr(modalias, "spi:ya,led-panel")) {
			snprintf(out, outlen, "/sys/bus/spi/devices/%s",
				 de->d_name);
			closedir(d);
			return 0;
		}
	}
	closedir(d);
	return -1;
}

/* ---------- framebuffer discovery ---------- */

static int try_open_fb(const char *path, int verbose, int *fd_out,
		       struct fb_fix_screeninfo *fix,
		       struct fb_var_screeninfo *var)
{
	int fd = open(path, O_RDWR);

	if (fd < 0) {
		if (verbose && errno != ENOENT)
			LOG("  %-12s : open failed: %s", path, strerror(errno));
		return -1;
	}
	if (ioctl(fd, FBIOGET_FSCREENINFO, fix)) {
		if (verbose)
			LOG("  %-12s : FSCREENINFO failed: %s",
			    path, strerror(errno));
		close(fd);
		return -1;
	}
	if (strncmp(fix->id, FB_ID_PREFIX, strlen(FB_ID_PREFIX)) != 0) {
		if (verbose)
			LOG("  %-12s : id '%s' — not ours", path, fix->id);
		close(fd);
		return -1;
	}
	if (ioctl(fd, FBIOGET_VSCREENINFO, var)) {
		LOG("  %-12s : VSCREENINFO failed: %s", path, strerror(errno));
		close(fd);
		return -1;
	}
	*fd_out = fd;
	return 0;
}

static int find_panel_fb(int *fd_out, struct fb_fix_screeninfo *fix,
			 struct fb_var_screeninfo *var)
{
	const char *candidates[] = {
		"/dev/fb0", "/dev/fb1", "/dev/fb2", "/dev/fb3", NULL
	};
	int i;

	LOG("scanning framebuffers for id prefix '%s'...", FB_ID_PREFIX);
	for (i = 0; candidates[i]; i++) {
		int fd;
		if (try_open_fb(candidates[i], 1, &fd, fix, var) == 0) {
			LOG("found panel at %s", candidates[i]);
			*fd_out = fd;
			return 0;
		}
	}

	/* fall back to scanning /dev/fb* via class dirs */
	for (i = 0; i < 8; i++) {
		char path[64];
		int fd;
		snprintf(path, sizeof(path), "/dev/fb%d", i);
		if (try_open_fb(path, 0, &fd, fix, var) == 0) {
			LOG("found panel at %s (late scan)", path);
			*fd_out = fd;
			return 0;
		}
	}
	return -1;
}

/* ---------- pattern helpers ---------- */

static uint8_t *fbmem;
static int fb_w, fb_h, fb_len;

static void push_frame(int fd, const char *what)
{
	long t0 = now_ms();
	if (fsync(fd)) {
		ERR("%s: fsync (fb_sync) failed", what);
		return;
	}
	LOG("  %-16s pushed in %ld ms", what, now_ms() - t0);
}

static void test_gray_ramp(int fd)
{
	int level;
	LOG("test: gray ramp (8 levels, full screen)");
	for (level = 0x20; level <= 0xff; level += 0x20) {
		memset(fbmem, level, fb_len);
		push_frame(fd, "gray ramp");
		usleep(250000);
	}
	PASS("gray ramp");
}

static void test_solid_colors(int fd)
{
	struct { uint8_t v; const char *n; } cols[] = {
		{ 0x00, "black" }, { 0xff, "white/max" },
		{ 0x40, "dim" }, { 0x80, "mid" },
	};
	size_t i;
	LOG("test: solid colors");
	for (i = 0; i < sizeof(cols)/sizeof(cols[0]); i++) {
		memset(fbmem, cols[i].v, fb_len);
		push_frame(fd, cols[i].n);
		usleep(300000);
	}
	PASS("solid colors");
}

static void test_checkerboard(int fd, int cell)
{
	int x, y;
	LOG("test: checkerboard cell=%dpx", cell);
	for (y = 0; y < fb_h; y++)
		for (x = 0; x < fb_w; x++)
			fbmem[y * fb_w + x] =
				((x / cell + y / cell) & 1) ? 0xff : 0x00;
	push_frame(fd, "checkerboard");
	usleep(500000);
	memset(fbmem, 0, fb_len);
	push_frame(fd, "clear");
	PASS("checkerboard");
}

static void test_border(int fd)
{
	int x, y;
	LOG("test: border + dark fill");
	memset(fbmem, 0x30, fb_len);
	for (y = 0; y < fb_h; y++)
		for (x = 0; x < fb_w; x++)
			if (x == 0 || y == 0 || x == fb_w - 1 || y == fb_h - 1)
				fbmem[y * fb_w + x] = 0xff;
	push_frame(fd, "border");
	usleep(500000);
	PASS("border");
}

static void test_moving_block(int fd, int frames)
{
	long t0 = now_ms();
	int f, x, y, bx, by, bw = 4, bh = 3;

	LOG("test: moving block, %d frames", frames);
	for (f = 0; f < frames; f++) {
		bx = (f * 2) % (fb_w - bw);
		by = (int)((f / 2) % (fb_h - bh));
		memset(fbmem, 0x10, fb_len);
		for (y = by; y < by + bh; y++)
			for (x = bx; x < bx + bw; x++)
				fbmem[y * fb_w + x] = 0xff;
		push_frame(fd, "anim");
		usleep(40000); /* ~25 fps */
	}
	LOG("  effective rate: %.1f fps",
	    frames * 1000.0 / (now_ms() - t0));
	PASS("moving block");
}

static void test_backlight(void)
{
	const char *names[] = { "atri_led_panel", NULL };
	char path[360];
	int i, v;

	LOG("backlight sweep:");
	for (i = 0; names[i]; i++) {
		DIR *d;
		struct dirent *de;
		char base[300];

		/* exact match first */
		snprintf(path, sizeof(path),
			 "/sys/class/backlight/%s/brightness", names[i]);
		if (access(path, W_OK) == 0) {
			for (v = 255; v >= 0; v -= 85) {
				int fd = open(path, O_WRONLY);
				if (fd < 0) {
					ERR("open %s", path);
					return;
				}
				dprintf(fd, "%d\n", v);
				close(fd);
				LOG("  brightness=%d", v);
				usleep(200000);
			}
			PASS("backlight sweep (%s)", names[i]);
			return;
		}
		/* otherwise scan */
		d = opendir("/sys/class/backlight");
		if (!d) continue;
		while ((de = readdir(d))) {
			if (de->d_name[0] == '.') continue;
			snprintf(base, sizeof(base),
				 "/sys/class/backlight/%s", de->d_name);
			dump_sysfs_attr(base, "max_brightness");
			dump_sysfs_attr(base, "actual_brightness");
			snprintf(path, sizeof(path), "%s/brightness", base);
			if (access(path, W_OK) == 0) {
				int fd = open(path, O_WRONLY);
				if (fd >= 0) {
					dprintf(fd, "200\n");
					close(fd);
					LOG("  set %s=200", path);
				}
			}
		}
		closedir(d);
	}
	LOG("backlight: not found (non-fatal)");
}

int main(int argc, char **argv)
{
	int fd = -1;
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	char spidev[512];

	clock_gettime(CLOCK_MONOTONIC, &t_prog_start);
	setbuf(stdout, NULL);

	LOG("=== atri-screen-test start ===");

	/* 1. locate the panel */
	if (find_panel_fb(&fd, &fix, &var))
		FAIL("framebuffer discovery",
		     "no fb with id prefix '%s' — driver probed?",
		     FB_ID_PREFIX);
	else
		PASS("framebuffer discovery (%s, id='%s')",
		     argc > 1 ? argv[1] : "/dev/fb?", fix.id);

	/* 2. geometry sanity */
	if (fd >= 0) {
		fb_w = var.xres; fb_h = var.yres;
		fb_len = fix.smem_len;
		LOG("geometry: %dx%d bpp=%u smem_len=%u line_length=%u",
		    fb_w, fb_h, var.bits_per_pixel,
		    fix.smem_len, fix.line_length);

		if (!((fb_w == EXP_W1 && fb_h == EXP_H1) ||
		      (fb_w == EXP_W2 && fb_h == EXP_H1)))
			FAIL("geometry", "unexpected resolution %dx%d "
			     "(expected %dx%d or %dx%d)",
			     fb_w, fb_h, EXP_W1, EXP_H1, EXP_W2, EXP_H1);
		else
			PASS("geometry");

		if (var.bits_per_pixel != 8)
			FAIL("bpp", "expected 8, got %u", var.bits_per_pixel);
		else
			PASS("bpp");

		fbmem = mmap(NULL, fb_len, PROT_READ | PROT_WRITE,
			     MAP_SHARED, fd, 0);
		if (fbmem == MAP_FAILED)
			ERR("mmap framebuffer");
		else
			PASS("mmap (%d bytes)", fb_len);
	}

	/* 3. driver/fPGA state over sysfs */
	if (find_spi_device_sysfs(spidev, sizeof(spidev)) == 0) {
		LOG("spi device: %s", spidev);
		dump_sysfs_attr(spidev, "fw_upd_status");
		dump_sysfs_attr(spidev, "crc_errors_master");
		dump_sysfs_attr(spidev, "crc_errors_slave");
		dump_sysfs_attr(spidev, "open_fb_count");
		dump_sysfs_attr(spidev, "frames_in_queue");
		dump_sysfs_attr(spidev, "frame_queue_overflows");
		dump_sysfs_attr(spidev, "show_debug_info");
	} else {
		LOG("panel spi device not bound (driver missing/not probed?)");
	}

	if (fd < 0 || fbmem == MAP_FAILED) {
		LOG("=== aborting pattern tests (no fb access) ===");
		goto done_nofb;
	}

	/* 4. patterns */
	test_solid_colors(fd);
	test_gray_ramp(fd);
	test_checkerboard(fd, 2);
	test_checkerboard(fd, 1);
	test_border(fd);
	test_moving_block(fd, 60);
	test_backlight();

	/* leave the screen dark */
	memset(fbmem, 0, fb_len);
	push_frame(fd, "final clear");

done_nofb:
	if (fd >= 0)
		close(fd);
	LOG("=== finished: %d failure(s), %ld ms total ===",
	    failures, now_ms() -
	    (t_prog_start.tv_sec * 1000 + t_prog_start.tv_nsec / 1000000));
	return failures ? 1 : 0;
}
