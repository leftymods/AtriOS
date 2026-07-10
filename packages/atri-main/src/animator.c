/* SPDX-License-Identifier: GPL-2.0 */
#include "animator.h"
#include "config.h"
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

static int cmp_frame(const struct dirent **a, const struct dirent **b)
{
    return strcmp((*a)->d_name, (*b)->d_name);
}

static int load_raw_frames(struct animation *anim, const char *dir)
{
    memset(anim, 0, sizeof(*anim));
    struct dirent **namelist = NULL;
    int n = scandir(dir, &namelist, NULL, cmp_frame);
    if (n < 0) return -1;

    anim->frame_size = SCREEN_FRAME_SIZE;
    anim->frames = malloc(n * anim->frame_size);
    if (!anim->frames) {
        free(namelist);
        return -1;
    }

    int count = 0;
    for (int i = 0; i < n; i++) {
        if (namelist[i]->d_name[0] == '.') continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, namelist[i]->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        if (read(fd, anim->frames + count * anim->frame_size, anim->frame_size) == anim->frame_size)
            count++;
        close(fd);
    }
    free(namelist);
    anim->frame_count = count;
    anim->loop = true;
    anim->frame_delay_ms = 100;
    return count > 0 ? 0 : -1;
}

static void free_anim(struct animation *anim)
{
    free(anim->frames);
    anim->frames = NULL;
    anim->frame_count = 0;
}

static void render_animation(struct animation *anim, struct screen *s, struct ring *r, uint64_t now_ms)
{
    if (anim->frame_count <= 0) return;
    if (now_ms - anim->last_frame_ms >= (uint64_t)anim->frame_delay_ms) {
        anim->current++;
        if (anim->current >= anim->frame_count) {
            if (anim->loop)
                anim->current = 0;
            else
                anim->current = anim->frame_count - 1;
        }
        anim->last_frame_ms = now_ms;
    }
    if (s)
        screen_render(s, anim->frames + anim->current * anim->frame_size);
    if (r) {
        for (int i = 0; i < RING_LEDS; i++) {
            int off = i * 3;
            ring_set_pixel(r, i,
                anim->frames[anim->current * anim->frame_size + off + 0],
                anim->frames[anim->current * anim->frame_size + off + 1],
                anim->frames[anim->current * anim->frame_size + off + 2]);
        }
    }
}

static void render_volume(struct animator *a, struct screen *s, struct ring *r)
{
    uint8_t frame[SCREEN_FRAME_SIZE];
    memset(frame, 0, sizeof(frame));
    int bars = (a->volume_level * SCREEN_WIDTH) / 100;
    for (int x = 0; x < bars && x < SCREEN_WIDTH; x++) {
        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            int off = (y * SCREEN_WIDTH + x) * 3;
            frame[off + 0] = 0;
            frame[off + 1] = 200;
            frame[off + 2] = 0;
        }
    }
    if (s) screen_render(s, frame);
    if (r) {
        int lit = (a->volume_level * RING_LEDS) / 100;
        for (int i = 0; i < RING_LEDS; i++) {
            if (i < lit)
                ring_set_pixel(r, i, 0, 200, 0);
            else
                ring_set_pixel(r, i, 0, 0, 0);
        }
    }
}

static void render_mute(struct screen *s, struct ring *r)
{
    uint8_t frame[SCREEN_FRAME_SIZE];
    memset(frame, 0, sizeof(frame));
    for (int y = 4; y < 12; y++) {
        for (int x = 4; x < 21; x++) {
            int off = (y * SCREEN_WIDTH + x) * 3;
            frame[off + 0] = 200;
            frame[off + 1] = 0;
            frame[off + 2] = 0;
        }
    }
    if (s) screen_render(s, frame);
    if (r) ring_set_all(r, 200, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Test patterns                                                       */
/* ------------------------------------------------------------------ */

static void set_pixel(uint8_t *frame, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT)
        return;
    int off = (y * SCREEN_WIDTH + x) * SCREEN_BPP;
    frame[off + 0] = r;
    frame[off + 1] = g;
    frame[off + 2] = b;
}

static void draw_snake(uint8_t *frame, int step)
{
    int len = 12;
    int head = step % (SCREEN_WIDTH * SCREEN_HEIGHT);

    for (int i = 0; i < len; i++) {
        int idx = (head - i + SCREEN_WIDTH * SCREEN_HEIGHT) % (SCREEN_WIDTH * SCREEN_HEIGHT);
        int px = idx % SCREEN_WIDTH;
        int py = idx / SCREEN_WIDTH;
        int intensity = 255 - (i * 220 / len);
        set_pixel(frame, px, py, 0, intensity, 0);
    }
}

static void draw_bounce(uint8_t *frame, int step)
{
    int x = step % (2 * (SCREEN_WIDTH - 1));
    if (x >= SCREEN_WIDTH)
        x = 2 * (SCREEN_WIDTH - 1) - x;
    int y = (step * 2) % (2 * (SCREEN_HEIGHT - 1));
    if (y >= SCREEN_HEIGHT)
        y = 2 * (SCREEN_HEIGHT - 1) - y;

    set_pixel(frame, x, y, 255, 0, 0);
    set_pixel(frame, x - 1, y, 120, 0, 0);
    set_pixel(frame, x + 1, y, 120, 0, 0);
    set_pixel(frame, x, y - 1, 120, 0, 0);
    set_pixel(frame, x, y + 1, 120, 0, 0);
}

static void draw_fill(uint8_t *frame, int step)
{
    int total = SCREEN_WIDTH * SCREEN_HEIGHT;
    int filled = (step * 5) % (total + 10);

    for (int i = 0; i < total && i < filled; i++) {
        int x = i % SCREEN_WIDTH;
        int y = i / SCREEN_WIDTH;
        uint8_t c = (uint8_t)(i * 255 / total);
        set_pixel(frame, x, y, c, c, c);
    }
}

static void draw_rainbow(uint8_t *frame, int step)
{
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            int h = (x + y + step * 2) % 768;
            uint8_t r, g, b;
            if (h < 256) {
                r = 255 - h; g = h; b = 0;
            } else if (h < 512) {
                r = 0; g = 511 - h; b = h - 256;
            } else {
                r = h - 512; g = 0; b = 767 - h;
            }
            set_pixel(frame, x, y, r, g, b);
        }
    }
}

static void draw_chase(uint8_t *frame, int step)
{
    int bar = step % SCREEN_WIDTH;

    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            int dist = abs(x - bar);
            uint8_t c = dist < 3 ? (255 - dist * 80) : 0;
            set_pixel(frame, x, y, c, c / 2, 0);
        }
    }
}

static void render_test_ring(struct ring *r, enum test_pattern p, int step)
{
    if (!r)
        return;

    for (int i = 0; i < RING_LEDS; i++) {
        switch (p) {
        case TEST_SNAKE: {
            int head = step % RING_LEDS;
            int dist = (head - i + RING_LEDS) % RING_LEDS;
            int c = (dist < 6) ? (255 - dist * 40) : 0;
            ring_set_pixel(r, i, 0, c, 0);
            break;
        }
        case TEST_BOUNCE: {
            int head = (step / 2) % (2 * RING_LEDS);
            if (head >= RING_LEDS)
                head = 2 * RING_LEDS - head;
            int dist = abs(head - i);
            int c = (dist < 3) ? (255 - dist * 80) : 0;
            ring_set_pixel(r, i, c, 0, 0);
            break;
        }
        case TEST_FILL: {
            int lit = (step * 2) % (RING_LEDS + 4);
            uint8_t c = (i < lit) ? 200 : 0;
            ring_set_pixel(r, i, c, c, 0);
            break;
        }
        case TEST_RAINBOW: {
            int h = (i * 768 / RING_LEDS + step * 20) % 768;
            uint8_t rr, gg, bb;
            if (h < 256) {
                rr = 255 - h; gg = h; bb = 0;
            } else if (h < 512) {
                rr = 0; gg = 511 - h; bb = h - 256;
            } else {
                rr = h - 512; gg = 0; bb = 767 - h;
            }
            ring_set_pixel(r, i, rr, gg, bb);
            break;
        }
        case TEST_CHASE: {
            int head = step % RING_LEDS;
            int dist = (head - i + RING_LEDS) % RING_LEDS;
            int c = (dist < 4) ? (255 - dist * 60) : 0;
            ring_set_pixel(r, i, c, c / 2, 0);
            break;
        }
        default:
            ring_set_pixel(r, i, 0, 0, 0);
        }
    }
}

static void render_test(struct animator *a, struct screen *s, struct ring *r, uint64_t now_ms)
{
    uint8_t frame[SCREEN_FRAME_SIZE];

    memset(frame, 0, sizeof(frame));

    if (now_ms - a->test_last_update_ms >= 80) {
        a->test_step++;
        a->test_last_update_ms = now_ms;
    }

    switch (a->test_pattern) {
    case TEST_SNAKE:
        draw_snake(frame, a->test_step);
        break;
    case TEST_BOUNCE:
        draw_bounce(frame, a->test_step);
        break;
    case TEST_FILL:
        draw_fill(frame, a->test_step);
        break;
    case TEST_RAINBOW:
        draw_rainbow(frame, a->test_step);
        break;
    case TEST_CHASE:
        draw_chase(frame, a->test_step);
        break;
    default:
        break;
    }

    if (s)
        screen_render(s, frame);
    if (r)
        render_test_ring(r, a->test_pattern, a->test_step);
}

int animator_init(struct animator *a, struct config *cfg)
{
    memset(a, 0, sizeof(*a));
    a->cfg = cfg;
    a->state = STATE_BOOT;
    a->state_entered_ms = get_ms();
    a->volume_level = 50;
    a->brightness = cfg->default_brightness;
    a->test_pattern = TEST_NONE;
    a->test_step = 0;
    a->test_last_update_ms = 0;

    char path[512];
    snprintf(path, sizeof(path), "%s/screen_raw/boot", cfg->animations_dir);
    load_raw_frames(&a->boot, path);
    snprintf(path, sizeof(path), "%s/screen_raw/clock", cfg->animations_dir);
    load_raw_frames(&a->idle, path);
    snprintf(path, sizeof(path), "%s/screen_raw/thinking", cfg->animations_dir);
    load_raw_frames(&a->thinking, path);
    snprintf(path, sizeof(path), "%s/screen_raw/alarm", cfg->animations_dir);
    load_raw_frames(&a->alarm, path);
    snprintf(path, sizeof(path), "%s/screen_raw/timer", cfg->animations_dir);
    load_raw_frames(&a->timer, path);
    snprintf(path, sizeof(path), "%s/screen_raw/bluetooth", cfg->animations_dir);
    load_raw_frames(&a->bluetooth, path);
    snprintf(path, sizeof(path), "%s/screen_raw/error", cfg->animations_dir);
    load_raw_frames(&a->error, path);

    return 0;
}

void animator_close(struct animator *a)
{
    free_anim(&a->boot);
    free_anim(&a->idle);
    free_anim(&a->thinking);
    free_anim(&a->alarm);
    free_anim(&a->timer);
    free_anim(&a->bluetooth);
    free_anim(&a->error);
}

void animator_set_state(struct animator *a, enum main_state state)
{
    if (a->state == state) return;
    a->prev_state = a->state;
    a->state = state;
    a->state_entered_ms = get_ms();
    log_msg(LOG_INFO, "state: %d -> %d", a->prev_state, state);
}

void animator_set_volume(struct animator *a, int level)
{
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    a->volume_level = level;
}

void animator_set_test_pattern(struct animator *a, enum test_pattern p)
{
    a->test_pattern = p;
    a->test_step = 0;
    a->test_last_update_ms = get_ms();
}

void animator_tick(struct animator *a, struct screen *s, struct ring *r, uint64_t now_ms)
{
    screen_set_brightness(s, a->brightness);

    switch (a->state) {
    case STATE_BOOT:
        render_animation(&a->boot, s, r, now_ms);
        if (now_ms - a->state_entered_ms > (uint64_t)a->cfg->boot_duration_ms)
            animator_set_state(a, STATE_IDLE);
        break;
    case STATE_IDLE:
        render_animation(&a->idle, s, r, now_ms);
        break;
    case STATE_VOLUME:
        render_volume(a, s, r);
        if (now_ms - a->state_entered_ms > (uint64_t)a->cfg->volume_timeout_ms)
            animator_set_state(a, STATE_IDLE);
        break;
    case STATE_MUTE:
        render_mute(s, r);
        if (now_ms - a->state_entered_ms > (uint64_t)a->cfg->volume_timeout_ms)
            animator_set_state(a, STATE_IDLE);
        break;
    case STATE_THINKING:
        render_animation(&a->thinking, s, r, now_ms);
        break;
    case STATE_ALARM:
        render_animation(&a->alarm, s, r, now_ms);
        break;
    case STATE_TIMER:
        render_animation(&a->timer, s, r, now_ms);
        break;
    case STATE_BLUETOOTH:
        render_animation(&a->bluetooth, s, r, now_ms);
        break;
    case STATE_ERROR:
        render_animation(&a->error, s, r, now_ms);
        break;
    case STATE_TEST:
        render_test(a, s, r, now_ms);
        break;
    }
}
