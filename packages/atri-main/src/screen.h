/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ATRI_MAIN_SCREEN_H
#define ATRI_MAIN_SCREEN_H

#include "common.h"

struct screen {
    int fd;
    uint8_t *fb;
    int width;
    int height;
    int bpp;
    int line_length;
    int backlight_fd;
    char backlight_path[256];
};

int screen_init(struct screen *s, const char *fb_path, const char *bl_path);
void screen_close(struct screen *s);
void screen_render(struct screen *s, const uint8_t *rgb);
int screen_set_brightness(struct screen *s, int brightness);

#endif
