/* SPDX-License-Identifier: GPL-2.0 */
#include "screen.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

static bool screen_match_id(const char *id)
{
    if (!id) return false;
    return (strncmp(id, "gowin_led", 9) == 0 ||
            strncmp(id, "GowinLED", 8) == 0 ||
            strncmp(id, "atri_led_panel", 14) == 0);
}

static int screen_open_fd(struct screen *s, int fd, const char *bl_path)
{
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        log_msg(LOG_ERR, "fb ioctl failed: %s", strerror(errno));
        return -1;
    }

    if (vinfo.xres != SCREEN_WIDTH || vinfo.yres != SCREEN_HEIGHT) {
        log_msg(LOG_ERR, "screen %s has wrong resolution %dx%d (expected %dx%d)",
                finfo.id, vinfo.xres, vinfo.yres, SCREEN_WIDTH, SCREEN_HEIGHT);
        return -1;
    }

    s->width = vinfo.xres;
    s->height = vinfo.yres;
    s->bpp = vinfo.bits_per_pixel / 8;
    s->line_length = finfo.line_length;
    s->mmap_len = finfo.smem_len;

    if (vinfo.bits_per_pixel != 8)
        log_msg(LOG_WARNING, "screen bpp=%d, expected 8", vinfo.bits_per_pixel);

    s->fb = mmap(NULL, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (s->fb == MAP_FAILED) {
        log_msg(LOG_ERR, "fb mmap failed: %s", strerror(errno));
        return -1;
    }

    s->backlight_fd = open(bl_path, O_WRONLY);
    if (s->backlight_fd < 0) {
        const char *bl_fallbacks[] = {
            "/sys/class/backlight/gowin-backlight/brightness",
            "/sys/class/backlight/atri_led_panel/brightness",
            "/sys/class/backlight/gowin_led/brightness",
            "/sys/class/backlight/led_screen/brightness",
            NULL
        };
        for (int i = 0; bl_fallbacks[i]; i++) {
            s->backlight_fd = open(bl_fallbacks[i], O_WRONLY);
            if (s->backlight_fd >= 0) {
                xstrlcpy(s->backlight_path, bl_fallbacks[i], sizeof(s->backlight_path));
                break;
            }
        }
    }
    if (s->backlight_fd < 0)
        log_msg(LOG_WARNING, "backlight %s not available: %s", bl_path, strerror(errno));

    log_msg(LOG_INFO, "screen '%s' %dx%d bpp=%d line=%d mapped",
            finfo.id, s->width, s->height, s->bpp, s->line_length);
    return 0;
}

static int screen_try_open(struct screen *s, const char *path,
                           const char *bl_path, bool filter_id)
{
    struct fb_fix_screeninfo finfo;
    int fd;

    fd = open(path, O_RDWR);
    if (fd < 0)
        return -1;

    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        close(fd);
        return -1;
    }

    if (filter_id && !screen_match_id(finfo.id)) {
        close(fd);
        return -1;
    }

    s->fd = fd;
    return screen_open_fd(s, fd, bl_path);
}

int screen_init(struct screen *s, const char *fb_path, const char *bl_path)
{
    memset(s, 0, sizeof(*s));
    xstrlcpy(s->backlight_path, bl_path, sizeof(s->backlight_path));

    /* Try the configured path first. */
    if (fb_path && fb_path[0]) {
        if (screen_try_open(s, fb_path, bl_path, false) == 0)
            return 0;
        log_msg(LOG_WARNING, "%s not usable, scanning for LED panel fb",
                fb_path);
    }

    /* Scan /dev/fb* for the LED panel fb. */
    for (int i = 0; i < 8; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/fb%d", i);
        if (screen_try_open(s, path, bl_path, true) == 0)
            return 0;
    }

    log_msg(LOG_ERR, "screen framebuffer not found");
    return -1;
}

void screen_close(struct screen *s)
{
    if (s->fb && s->fb != MAP_FAILED)
        munmap(s->fb, s->mmap_len);
    if (s->fd >= 0)
        close(s->fd);
    if (s->backlight_fd >= 0)
        close(s->backlight_fd);
}

void screen_render(struct screen *s, const uint8_t *rgb)
{
    if (!s->fb) return;
    for (int y = 0; y < s->height && y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < s->width && x < SCREEN_WIDTH; x++) {
            int src = (y * SCREEN_WIDTH + x) * SCREEN_BPP;
            int dst = y * s->line_length + x * s->bpp;
            if (s->bpp >= 3) {
                s->fb[dst + 0] = rgb[src + 0];
                s->fb[dst + 1] = rgb[src + 1];
                s->fb[dst + 2] = rgb[src + 2];
            } else if (s->bpp == 1) {
                s->fb[dst] = (rgb[src] | rgb[src + 1] | rgb[src + 2]) > 128 ? 0xFF : 0;
            }
        }
    }
}

int screen_set_brightness(struct screen *s, int brightness)
{
    if (s->backlight_fd < 0) return -1;
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%d\n", brightness);
    return write(s->backlight_fd, buf, n) < 0 ? -1 : 0;
}
