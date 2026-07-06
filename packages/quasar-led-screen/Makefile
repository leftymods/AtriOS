CC ?= gcc
CFLAGS = -O2 -Wall -I./include
LDFLAGS = -lm
LIB_SRC = src/quasar_screen.c
LIB_OBJ = quasar_screen.o
LIB = libquasar_screen.a

SCREEN_BINS = quasar_led_test quasar_led_text quasar_led_demo quasar_led_info
ALL_BINS = $(SCREEN_BINS) quasar_led_ctl

all: $(ALL_BINS)

$(LIB): $(LIB_OBJ)
	ar rcs $@ $^

quasar_screen.o: src/quasar_screen.c include/quasar_screen.h
	$(CC) $(CFLAGS) -c $< -o $@

quasar_led_test: src/quasar_led_test.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -lquasar_screen $(LDFLAGS) -o $@

quasar_led_text: src/quasar_led_text.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -lquasar_screen $(LDFLAGS) -o $@

quasar_led_demo: src/quasar_led_demo.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -lquasar_screen $(LDFLAGS) -o $@

quasar_led_info: src/quasar_led_info.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -lquasar_screen $(LDFLAGS) -o $@

clean:
	rm -f *.o $(LIB) $(ALL_BINS)

.PHONY: all clean
