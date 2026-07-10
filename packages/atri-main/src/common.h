/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ATRI_MAIN_COMMON_H
#define ATRI_MAIN_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <syslog.h>
#include <time.h>

#define ATRI_MAIN_VERSION "0.1"
#define SCREEN_WIDTH 25
#define SCREEN_HEIGHT 16
#define SCREEN_BPP 3
#define SCREEN_FRAME_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT * SCREEN_BPP)
#define RING_LEDS 24

enum main_state {
    STATE_BOOT,
    STATE_IDLE,
    STATE_VOLUME,
    STATE_MUTE,
    STATE_THINKING,
    STATE_ALARM,
    STATE_TIMER,
    STATE_BLUETOOTH,
    STATE_ERROR,
};

void log_init(bool use_syslog);
void log_close(void);
void log_msg(int priority, const char *fmt, ...);

static inline size_t xstrlcpy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size) {
        size_t copy = len < size ? len : size - 1;
        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return len;
}

static inline uint64_t get_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#endif
