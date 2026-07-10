/* SPDX-License-Identifier: GPL-2.0 */
#include "input.h"
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>

#define MAX_INPUTS 8

struct input_ctx {
    struct animator *animator;
    int fds[MAX_INPUTS];
    int count;
};

static int open_input_devices(struct input_ctx *ctx)
{
    glob_t gl;
    if (glob("/dev/input/event*", 0, NULL, &gl) != 0)
        return 0;
    for (size_t i = 0; i < gl.gl_pathc && ctx->count < MAX_INPUTS; i++) {
        int fd = open(gl.gl_pathv[i], O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        ctx->fds[ctx->count++] = fd;
    }
    globfree(&gl);
    return ctx->count;
}

struct input_ctx *input_init(struct animator *a)
{
    struct input_ctx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->animator = a;
    if (open_input_devices(ctx) == 0)
        log_msg(LOG_WARNING, "no input devices opened");
    return ctx;
}

void input_close(struct input_ctx *ctx)
{
    if (!ctx) return;
    for (int i = 0; i < ctx->count; i++)
        close(ctx->fds[i]);
    free(ctx);
}

void input_poll(struct input_ctx *ctx)
{
    if (!ctx) return;
    struct input_event ev;
    for (int i = 0; i < ctx->count; i++) {
        while (read(ctx->fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type != EV_KEY || ev.value != 1) continue;
            switch (ev.code) {
            case KEY_VOLUMEUP:
                animator_set_volume(ctx->animator, ctx->animator->volume_level + 5);
                animator_set_state(ctx->animator, STATE_VOLUME);
                break;
            case KEY_VOLUMEDOWN:
                animator_set_volume(ctx->animator, ctx->animator->volume_level - 5);
                animator_set_state(ctx->animator, STATE_VOLUME);
                break;
            case KEY_MICMUTE:
                animator_set_state(ctx->animator, STATE_MUTE);
                break;
            case KEY_VOICECOMMAND:
                animator_set_state(ctx->animator, STATE_THINKING);
                break;
            }
        }
    }
}
