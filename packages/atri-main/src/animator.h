/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ATRI_MAIN_ANIMATOR_H
#define ATRI_MAIN_ANIMATOR_H

#include "common.h"
#include "screen.h"
#include "ring.h"

struct animation {
    uint8_t *frames;
    int frame_count;
    int frame_size;
    int current;
    uint64_t last_frame_ms;
    int frame_delay_ms;
    bool loop;
};

struct animator {
    struct config *cfg;
    enum main_state state;
    enum main_state prev_state;
    uint64_t state_entered_ms;

    struct animation boot;
    struct animation idle;
    struct animation volume;
    struct animation mute;
    struct animation thinking;
    struct animation alarm;
    struct animation timer;
    struct animation bluetooth;
    struct animation error;

    int volume_level;
    int brightness;
};

int animator_init(struct animator *a, struct config *cfg);
void animator_close(struct animator *a);
void animator_tick(struct animator *a, struct screen *s, struct ring *r, uint64_t now_ms);
void animator_set_state(struct animator *a, enum main_state state);
void animator_set_volume(struct animator *a, int level);

#endif
