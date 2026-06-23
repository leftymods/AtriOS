CC = gcc
CFLAGS = -O2 -Wall
LDFLAGS = 

all: quasar_led_test

quasar_led_test: src/quasar_led_test.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f quasar_led_test

.PHONY: all clean