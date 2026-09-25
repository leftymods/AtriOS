CC ?= gcc
CFLAGS = -O2 -Wall -I./include -D_GNU_SOURCE
LDFLAGS = -static -lm

# Libraries
LED_LIB_SRC = src/atri_led.c src/atri_led_anim.c
LED_LIB_OBJ = atri_led.o atri_led_anim.o
LED_LIB = libatri_led.a

# LED Binaries
LED_DAEMON_BIN = atri-led
LED_CTL_BIN = atri-led-ctl

ALL_BINS = $(LED_DAEMON_BIN) $(LED_CTL_BIN)
TEST_BIN = test_atri_led

all: $(LED_LIB) $(ALL_BINS)

# Library
$(LED_LIB): $(LED_LIB_OBJ)
	ar rcs $@ $^

atri_led.o: src/atri_led.c include/atri_led.h
	$(CC) $(CFLAGS) -c $< -o $@

atri_led_anim.o: src/atri_led_anim.c include/atri_led.h include/atri_led_anim.h
	$(CC) $(CFLAGS) -c $< -o $@

# Binaries
$(LED_DAEMON_BIN): src/atriled.c $(LED_LIB) include/atri_led.h include/atri_led_anim.h
	$(CC) $(CFLAGS) -I./include $< -L. -latri_led $(LDFLAGS) -o $@

$(LED_CTL_BIN): src/atri_led_ctl.c $(LED_LIB) include/atri_led.h
	$(CC) $(CFLAGS) $< -L. -latri_led $(LDFLAGS) -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): tests/test_atri_led.c $(LED_LIB) include/atri_led.h include/atri_led_anim.h
	$(CC) $(CFLAGS) $< -L. -latri_led $(LDFLAGS) -o $@

install: all
	mkdir -p $(DESTDIR)/usr/lib
	mkdir -p $(DESTDIR)/usr/bin
	mkdir -p $(DESTDIR)/usr/include
	mkdir -p $(DESTDIR)/lib/systemd/system
	mkdir -p $(DESTDIR)/etc/atriled/animations
	mkdir -p $(DESTDIR)/lib/udev/rules.d
	cp $(LED_LIB) $(DESTDIR)/usr/lib/
	cp $(LED_DAEMON_BIN) $(DESTDIR)/usr/bin/
	cp $(LED_CTL_BIN) $(DESTDIR)/usr/bin/
	# Backward-compatible and canonical symlinks
	ln -sf $(LED_DAEMON_BIN) $(DESTDIR)/usr/bin/atrled
	ln -sf $(LED_CTL_BIN) $(DESTDIR)/usr/bin/atrledctl
	cp include/atri_led.h $(DESTDIR)/usr/include/
	cp include/atri_led_anim.h $(DESTDIR)/usr/include/
	cp etc/atriled/animations/*.led $(DESTDIR)/etc/atriled/animations/ 2>/dev/null || true
	cp etc/atriled/animations/*.anim $(DESTDIR)/etc/atriled/animations/ 2>/dev/null || true
	cp etc/udev/rules.d/99-atri-led.rules $(DESTDIR)/lib/udev/rules.d/ 2>/dev/null || true
	cp lib/systemd/system/atri-led*.service $(DESTDIR)/lib/systemd/system/ 2>/dev/null || true

clean:
	rm -f *.o $(LED_LIB) $(ALL_BINS) $(TEST_BIN)
