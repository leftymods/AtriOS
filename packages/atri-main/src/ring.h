/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ATRI_MAIN_RING_H
#define ATRI_MAIN_RING_H

#include "common.h"

struct ring_led {
    int index;
    char red_path[256];
    char green_path[256];
    char blue_path[256];
    char brightness_path[256];
    bool has_colors;
};

struct ring {
    struct ring_led leds[RING_LEDS];
    int num_leds;
};

int ring_init(struct ring *r, const char *pattern);
void ring_close(struct ring *r);
void ring_set_all(struct ring *r, uint8_t r_c, uint8_t g_c, uint8_t b_c);
void ring_set_pixel(struct ring *r, int idx, uint8_t r_c, uint8_t g_c, uint8_t b_c);

#endif
