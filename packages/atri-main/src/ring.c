/* SPDX-License-Identifier: GPL-2.0 */
#include "ring.h"
#include <fcntl.h>
#include <glob.h>

static int write_file(const char *path, const char *val)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    int ret = write(fd, val, strlen(val));
    close(fd);
    return ret < 0 ? -1 : 0;
}

static int probe_led(struct ring_led *led, const char *base)
{
    char path[512];
    xstrlcpy(led->brightness_path, base, sizeof(led->brightness_path));
    led->has_colors = false;

    snprintf(path, sizeof(path), "%s/colors", base);
    if (access(path, F_OK) == 0) {
        xstrlcpy(led->red_path, path, sizeof(led->red_path));
        led->has_colors = true;
        return 0;
    }

    snprintf(path, sizeof(path), "%s/multi_intensity", base);
    if (access(path, F_OK) == 0) {
        xstrlcpy(led->red_path, path, sizeof(led->red_path));
        led->has_colors = true;
        return 0;
    }

    return 0;
}

static int led_name_index(const char *name)
{
    const char *p = strrchr(name, '/');
    if (p) name = p + 1;
    if (strncmp(name, "ring", 4) != 0) return -1;
    int idx = atoi(name + 4);
    if (idx < 0 || idx >= RING_LEDS) return -1;
    return idx;
}

int ring_init(struct ring *r, const char *pattern)
{
    memset(r, 0, sizeof(*r));
    glob_t gl;
    if (glob(pattern, 0, NULL, &gl) != 0) {
        log_msg(LOG_WARNING, "no ring LEDs found matching %s", pattern);
        return -1;
    }

    for (size_t i = 0; i < gl.gl_pathc; i++) {
        int idx = led_name_index(gl.gl_pathv[i]);
        if (idx < 0 || idx >= RING_LEDS) continue;
        if (probe_led(&r->leds[idx], gl.gl_pathv[i]) < 0) continue;
        r->leds[idx].index = idx;
        r->num_leds++;
    }
    globfree(&gl);

    log_msg(LOG_INFO, "ring: %d LEDs detected", r->num_leds);
    return r->num_leds > 0 ? 0 : -1;
}

void ring_close(struct ring *r)
{
    (void)r;
}

void ring_set_pixel(struct ring *r, int idx, uint8_t r_c, uint8_t g_c, uint8_t b_c)
{
    if (idx < 0 || idx >= RING_LEDS) return;
    struct ring_led *led = &r->leds[idx];
    if (!led->brightness_path[0]) return;

    char buf[64];
    if (led->has_colors) {
        if (strstr(led->red_path, "multi_intensity"))
            snprintf(buf, sizeof(buf), "%d %d %d\n", r_c, g_c, b_c);
        else
            snprintf(buf, sizeof(buf), "%d %d %d\n", r_c, g_c, b_c);
        write_file(led->red_path, buf);
    }
    int gray = (r_c + g_c + b_c) / 3;
    snprintf(buf, sizeof(buf), "%d\n", gray);
    write_file(led->brightness_path, buf);
}

void ring_set_all(struct ring *r, uint8_t r_c, uint8_t g_c, uint8_t b_c)
{
    for (int i = 0; i < RING_LEDS; i++)
        ring_set_pixel(r, i, r_c, g_c, b_c);
}
