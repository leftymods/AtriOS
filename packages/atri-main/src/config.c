/* SPDX-License-Identifier: GPL-2.0 */
#include "config.h"
#include <ctype.h>

void config_defaults(struct config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    xstrlcpy(cfg->screen_fb_path, "/dev/fb0", sizeof(cfg->screen_fb_path));
    xstrlcpy(cfg->backlight_path, "/sys/class/backlight/atri_led_panel/brightness", sizeof(cfg->backlight_path));
    xstrlcpy(cfg->ring_leds_path, "/sys/class/leds/ring*", sizeof(cfg->ring_leds_path));
    xstrlcpy(cfg->animations_dir, "/usr/share/atri-main/animations", sizeof(cfg->animations_dir));
    cfg->default_brightness = 200;
    cfg->idle_timeout_ms = 30000;
    cfg->volume_timeout_ms = 2000;
    cfg->boot_duration_ms = 3000;
    cfg->use_syslog = true;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    if (!*s) return s;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) *e-- = '\0';
    return s;
}

int config_load(struct config *cfg, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        log_msg(LOG_WARNING, "config %s not found, using defaults", path);
        return 0;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = strchr(line, '#');
        if (p) *p = '\0';
        char *key = trim(line);
        if (!*key) continue;
        char *val = strchr(key, '=');
        if (!val) continue;
        *val++ = '\0';
        key = trim(key);
        val = trim(val);

        if (strcmp(key, "screen_fb_path") == 0)
            xstrlcpy(cfg->screen_fb_path, val, sizeof(cfg->screen_fb_path));
        else if (strcmp(key, "backlight_path") == 0)
            xstrlcpy(cfg->backlight_path, val, sizeof(cfg->backlight_path));
        else if (strcmp(key, "ring_leds_path") == 0)
            xstrlcpy(cfg->ring_leds_path, val, sizeof(cfg->ring_leds_path));
        else if (strcmp(key, "animations_dir") == 0)
            xstrlcpy(cfg->animations_dir, val, sizeof(cfg->animations_dir));
        else if (strcmp(key, "default_brightness") == 0)
            cfg->default_brightness = atoi(val);
        else if (strcmp(key, "idle_timeout_ms") == 0)
            cfg->idle_timeout_ms = atoi(val);
        else if (strcmp(key, "volume_timeout_ms") == 0)
            cfg->volume_timeout_ms = atoi(val);
        else if (strcmp(key, "boot_duration_ms") == 0)
            cfg->boot_duration_ms = atoi(val);
        else if (strcmp(key, "use_syslog") == 0)
            cfg->use_syslog = atoi(val) != 0;
    }
    fclose(f);
    return 0;
}
