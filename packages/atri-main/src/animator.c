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

int animator_init(struct animator *a, struct config *cfg)
{
    memset(a, 0, sizeof(*a));
    a->cfg = cfg;
    a->state = STATE_BOOT;
    a->state_entered_ms = get_ms();
    a->volume_level = 50;
    a->brightness = cfg->default_brightness;

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
    }
}
