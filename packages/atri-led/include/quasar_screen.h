#ifndef QUASAR_SCREEN_H
#define QUASAR_SCREEN_H

#include <stdint.h>
#include <stddef.h>

#define SCREEN_WIDTH  25
#define SCREEN_HEIGHT 16
#define SCREEN_PIXELS (SCREEN_WIDTH * SCREEN_HEIGHT)
#define SCREEN_BYTES  ((SCREEN_PIXELS + 7) / 8)

typedef struct {
	int spi_fd;
	int gpio_reset;
	uint8_t fb[SCREEN_BYTES];
} quasar_screen_t;

int  screen_open(quasar_screen_t *scr, const char *spi_dev);
void screen_close(quasar_screen_t *scr);
void screen_reset(quasar_screen_t *scr);
void screen_clear(quasar_screen_t *scr);
void screen_pixel(quasar_screen_t *scr, int x, int y, int on);
void screen_rect(quasar_screen_t *scr, int x, int y, int w, int h, int on);
void screen_line(quasar_screen_t *scr, int x0, int y0, int x1, int y1, int on);
void screen_text(quasar_screen_t *scr, int x, int y, const char *text, int on);
void screen_flush(quasar_screen_t *scr);
void screen_send_raw(quasar_screen_t *scr, const uint8_t *data, int len);

#endif
