/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ATRI_MAIN_INPUT_H
#define ATRI_MAIN_INPUT_H

#include "common.h"
#include "animator.h"

struct input_ctx;

struct input_ctx *input_init(struct animator *a);
void input_close(struct input_ctx *ctx);
void input_poll(struct input_ctx *ctx);

#endif
