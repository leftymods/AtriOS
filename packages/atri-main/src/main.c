/* SPDX-License-Identifier: GPL-2.0 */
#include "common.h"
#include "config.h"
#include "screen.h"
#include "ring.h"
#include "animator.h"
#include "input.h"
#include <signal.h>
#include <getopt.h>

static volatile bool running = true;

static void signal_handler(int sig)
{
    (void)sig;
    running = false;
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-c config] [-f] [-h] [-t pattern]\n", prog);
    fprintf(stderr, "  -c config   path to config file\n");
    fprintf(stderr, "  -f          run in foreground\n");
    fprintf(stderr, "  -t pattern  test pattern: snake, bounce, fill, rainbow, chase\n");
    fprintf(stderr, "  -h          show this help\n");
}

static enum test_pattern parse_test_pattern(const char *name)
{
    if (!strcmp(name, "snake"))   return TEST_SNAKE;
    if (!strcmp(name, "bounce"))  return TEST_BOUNCE;
    if (!strcmp(name, "fill"))    return TEST_FILL;
    if (!strcmp(name, "rainbow")) return TEST_RAINBOW;
    if (!strcmp(name, "chase"))   return TEST_CHASE;
    return TEST_NONE;
}

int main(int argc, char **argv)
{
    const char *config_path = "/etc/atri-main/main.conf";
    bool foreground = false;
    enum test_pattern test_pattern = TEST_NONE;
    int opt;

    while ((opt = getopt(argc, argv, "c:ft:h")) != -1) {
        switch (opt) {
        case 'c': config_path = optarg; break;
        case 'f': foreground = true; break;
        case 't': test_pattern = parse_test_pattern(optarg); break;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 1;
        }
    }

    struct config cfg;
    config_defaults(&cfg);
    config_load(&cfg, config_path);

    if (!foreground) {
        if (daemon(0, 0) < 0) {
            fprintf(stderr, "daemon failed: %s\n", strerror(errno));
            return 1;
        }
    }

    log_init(cfg.use_syslog);
    log_msg(LOG_INFO, "atri-main %s starting", ATRI_MAIN_VERSION);

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    struct screen scr;
    struct ring rng;
    struct animator anim;
    struct input_ctx *input = NULL;

    if (screen_init(&scr, cfg.screen_fb_path, cfg.backlight_path) < 0) {
        log_msg(LOG_WARNING, "screen not available, continuing without display");
    }

    if (ring_init(&rng, cfg.ring_leds_path) < 0) {
        log_msg(LOG_WARNING, "ring not available, continuing without ring");
    }

    animator_init(&anim, &cfg);
    if (test_pattern != TEST_NONE) {
        animator_set_test_pattern(&anim, test_pattern);
        animator_set_state(&anim, STATE_TEST);
    }
    input = input_init(&anim);

    while (running) {
        uint64_t now = get_ms();
        input_poll(input);
        animator_tick(&anim, &scr, &rng, now);
        usleep(20000);
    }

    log_msg(LOG_INFO, "atri-main shutting down");
    input_close(input);
    animator_close(&anim);
    ring_close(&rng);
    screen_close(&scr);
    log_close();
    return 0;
}
