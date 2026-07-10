/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ATRI_MAIN_CONFIG_H
#define ATRI_MAIN_CONFIG_H

#include "common.h"

struct config {
    char screen_fb_path[256];
    char backlight_path[256];
    char ring_leds_path[256];
    char animations_dir[256];
    int default_brightness;
    int idle_timeout_ms;
    int volume_timeout_ms;
    int boot_duration_ms;
    bool use_syslog;
};

int config_load(struct config *cfg, const char *path);
void config_defaults(struct config *cfg);

#endif
