/* SPDX-License-Identifier: GPL-2.0 */
#include "screen.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

int screen_init(struct screen *s, const char *fb_path, const char *bl_path)
{
    memset(s, 0, sizeof(*s));
    xstrlcpy(s->backlight_path, bl_path, sizeof(s->backlight_path));

    s->fd = open(fb_path, O_RDWR);
    if (s->fd < 0) {
        log_msg(LOG_ERR, "failed to open %s: %s", fb_path, strerror(errno));
        return -1;
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(s->fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(s->fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        log_msg(LOG_ERR, "fb ioctl failed: %s", strerror(errno));
        close(s->fd);
        return -1;
    }

    s->width = vinfo.xres;
    s->height = vinfo.yres;
    s->bpp = vinfo.bits_per_pixel / 8;
    s->line_length = finfo.line_length;

    s->fb = mmap(NULL, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, s->fd, 0);
    if (s->fb == MAP_FAILED) {
        log_msg(LOG_ERR, "fb mmap failed: %s", strerror(errno));
        close(s->fd);
        return -1;
    }

    s->backlight_fd = open(bl_path, O_WRONLY);
    if (s->backlight_fd < 0)
        log_msg(LOG_WARNING, "backlight %s not available: %s", bl_path, strerror(errno));

    log_msg(LOG_INFO, "screen %dx%d bpp=%d line=%d mapped", s->width, s->height, s->bpp, s->line_length);
    return 0;
}

void screen_close(struct screen *s)
{
    if (s->fb && s->fb != MAP_FAILED)
        munmap(s->fb, s->line_length * s->height);
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
