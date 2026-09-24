CC ?= gcc
CFLAGS = -O2 -Wall -I./include -D_GNU_SOURCE
LDFLAGS = -static -lm
ASOUND_LIBS = -lasound

# Libraries
LED_LIB_SRC = src/atri_led.c src/atri_led_anim.c
LED_LIB_OBJ = atri_led.o atri_led_anim.o
LED_LIB = libatri_led.a

SCR_LIB_SRC = src/quasar_screen.c
SCR_LIB_OBJ = quasar_screen.o
SCR_LIB = libquasar_screen.a

# Core Unified Binaries
LED_DAEMON_BIN = atri-led
LED_CTL_BIN = atri-led-ctl
VOL_BIN = atrivolume
SOUND_TEST_BIN = atri-sound-test
MATRIX_BIN = atri-matrix
HWPROBE_BIN = atri-hwprobe
ZGBIN = atri-zigbee
WIFI_DIAG_BIN = atri-wifi-diag
WIFI_INIT_BIN = atri-wireless-init
DISP_DAEMON_BIN = atri-displayd
DISP_CLI_BIN = atri-display

ALL_BINS = $(LED_DAEMON_BIN) $(LED_CTL_BIN) $(MATRIX_BIN) $(HWPROBE_BIN) $(ZGBIN) $(WIFI_DIAG_BIN) $(WIFI_INIT_BIN) $(DISP_DAEMON_BIN) $(DISP_CLI_BIN)

TEST_BIN = test_atri_led

# Check if -lasound is available for target (cross-compile may lack it)
ASOUND_AVAIL := $(shell echo 'int main(){return 0;}' | $(CC) $(ASOUND_LIBS) -x c - -o /dev/null 2>/dev/null && echo yes)

all: $(LED_LIB) $(SCR_LIB) $(ALL_BINS)
ifneq ($(ASOUND_AVAIL),yes)
	@echo "==> -lasound not available for target, skipping $(VOL_BIN) and $(SOUND_TEST_BIN)"
else
	@$(MAKE) $(VOL_BIN)
	@$(MAKE) $(SOUND_TEST_BIN)
endif

# Libraries
$(LED_LIB): $(LED_LIB_OBJ)
	ar rcs $@ $^

atri_led.o: src/atri_led.c include/atri_led.h
	$(CC) $(CFLAGS) -c $< -o $@

atri_led_anim.o: src/atri_led_anim.c include/atri_led.h include/atri_led_anim.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SCR_LIB): $(SCR_LIB_OBJ)
	ar rcs $@ $^

quasar_screen.o: src/quasar_screen.c include/quasar_screen.h
	$(CC) $(CFLAGS) -c $< -o $@

# Unified Binaries
$(LED_DAEMON_BIN): src/atriled.c $(LED_LIB) include/atri_led.h include/atri_led_anim.h
	$(CC) $(CFLAGS) -I./include $< -L. -latri_led $(LDFLAGS) -o $@

$(LED_CTL_BIN): src/atri_led_ctl.c $(LED_LIB) include/atri_led.h
	$(CC) $(CFLAGS) $< -L. -latri_led $(LDFLAGS) -o $@

$(VOL_BIN): src/atrivolume.c $(LED_LIB) include/atri_led.h
	$(CC) $(CFLAGS) $< -L. -latri_led $(ASOUND_LIBS) $(LDFLAGS) -o $@

$(SOUND_TEST_BIN): src/atri_sound_test.c
	$(CC) $(CFLAGS) $< $(ASOUND_LIBS) $(LDFLAGS) -o $@

$(MATRIX_BIN): src/atri_matrix.c $(SCR_LIB) include/quasar_screen.h
	$(CC) $(CFLAGS) -I./include $< -L. -lquasar_screen $(LDFLAGS) -o $@

$(HWPROBE_BIN): src/atri_hwprobe.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

$(ZGBIN): src/atri_zigbee.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

$(WIFI_DIAG_BIN): src/atri_wifi_diag.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

$(WIFI_INIT_BIN): src/atri_wireless_init.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

$(DISP_DAEMON_BIN): src/atri_displayd.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

$(DISP_CLI_BIN): src/atri_display_cli.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

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
	cp $(LED_LIB) $(DESTDIR)/usr/lib/
	cp $(SCR_LIB) $(DESTDIR)/usr/lib/
	cp $(LED_DAEMON_BIN) $(DESTDIR)/usr/bin/
	cp $(LED_CTL_BIN) $(DESTDIR)/usr/bin/
	cp $(MATRIX_BIN) $(DESTDIR)/usr/bin/
	cp $(HWPROBE_BIN) $(DESTDIR)/usr/bin/
	cp $(ZGBIN) $(DESTDIR)/usr/bin/
	cp $(WIFI_DIAG_BIN) $(DESTDIR)/usr/bin/
	cp $(WIFI_INIT_BIN) $(DESTDIR)/usr/bin/
	cp $(DISP_DAEMON_BIN) $(DESTDIR)/usr/bin/
	cp $(DISP_CLI_BIN) $(DESTDIR)/usr/bin/
	# Backward-compatible and canonical symlinks
	ln -sf $(LED_DAEMON_BIN) $(DESTDIR)/usr/bin/atrled
	ln -sf $(LED_CTL_BIN) $(DESTDIR)/usr/bin/atrledctl
	ln -sf $(WIFI_DIAG_BIN) $(DESTDIR)/usr/bin/atri-wireless
	ln -sf $(MATRIX_BIN) $(DESTDIR)/usr/bin/atri-screen-test
	ln -sf $(MATRIX_BIN) $(DESTDIR)/usr/bin/atri-screen
	ln -sf $(MATRIX_BIN) $(DESTDIR)/usr/bin/quasar_led_ctl
	ln -sf $(MATRIX_BIN) $(DESTDIR)/usr/bin/quasar_led_test
	ln -sf $(MATRIX_BIN) $(DESTDIR)/usr/bin/quasar_led_demo
	ln -sf $(MATRIX_BIN) $(DESTDIR)/usr/bin/quasar_led_text
	ln -sf $(MATRIX_BIN) $(DESTDIR)/usr/bin/quasar_led_info
	ln -sf $(MATRIX_BIN) $(DESTDIR)/usr/bin/atriled-screen
	ln -sf $(HWPROBE_BIN) $(DESTDIR)/usr/bin/atri-pcba
	ln -sf $(HWPROBE_BIN) $(DESTDIR)/usr/bin/atri-als
	ln -sf $(HWPROBE_BIN) $(DESTDIR)/usr/bin/atri-buttons
	ln -sf $(HWPROBE_BIN) $(DESTDIR)/usr/bin/atri-knob
ifneq ($(ASOUND_AVAIL),yes)
	@echo "==> skipping audio bins install (not built)"
else
	cp $(VOL_BIN) $(DESTDIR)/usr/bin/
	ln -sf $(VOL_BIN) $(DESTDIR)/usr/bin/atri-volume
	cp $(SOUND_TEST_BIN) $(DESTDIR)/usr/bin/
	ln -sf $(SOUND_TEST_BIN) $(DESTDIR)/usr/bin/atrisound-test
endif
	cp include/atri_led.h $(DESTDIR)/usr/include/
	cp include/atri_led_anim.h $(DESTDIR)/usr/include/
	cp include/quasar_screen.h $(DESTDIR)/usr/include/
	cp etc/atriled/animations/*.led $(DESTDIR)/etc/atriled/animations/
	cp etc/atriled/animations/*.anim $(DESTDIR)/etc/atriled/animations/ 2>/dev/null || true
	mkdir -p $(DESTDIR)/lib/firmware
	cp lib/firmware/*.manifest $(DESTDIR)/lib/firmware/
	mkdir -p $(DESTDIR)/lib/udev/rules.d
	cp etc/udev/rules.d/*.rules $(DESTDIR)/lib/udev/rules.d/
	cp systemd/*.service $(DESTDIR)/lib/systemd/system/
ifneq ($(ASOUND_AVAIL),yes)
	rm -f $(DESTDIR)/lib/systemd/system/atrivolume.service
endif

clean:
	rm -f *.o $(LED_LIB) $(SCR_LIB) $(ALL_BINS) $(VOL_BIN) $(SOUND_TEST_BIN)

.PHONY: all install clean
