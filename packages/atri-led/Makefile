CC ?= gcc
CFLAGS = -O2 -Wall -I./include -D_GNU_SOURCE
LDFLAGS = -lm
ASOUND_LIBS = -lasound

# LED ring library
LED_LIB_SRC = src/atri_led.c src/atri_led_anim.c
LED_LIB_OBJ = atri_led.o atri_led_anim.o
LED_LIB = libatri_led.a

# SPI screen library
SCR_LIB_SRC = src/quasar_screen.c
SCR_LIB_OBJ = quasar_screen.o
SCR_LIB = libquasar_screen.a

# Binaries
CTL_BIN = atrledctl
MAIN_BIN = atrled
VOL_BIN = atrivolume
SCR_BIN = atriled-screen
QCTL_BIN = quasar_led_ctl
QTST_BIN = quasar_led_test
QTXT_BIN = quasar_led_text
QDEMO_BIN = quasar_led_demo
QINFO_BIN = quasar_led_info
SCREEN_BINS = $(QTST_BIN) $(QTXT_BIN) $(QDEMO_BIN) $(QINFO_BIN)
ALL_BINS = $(CTL_BIN) $(MAIN_BIN) $(SCR_BIN) $(QCTL_BIN) $(SCREEN_BINS)

# Check if -lasound is available for target (cross-compile may lack it)
ASOUND_AVAIL := $(shell echo 'int main(){return 0;}' | $(CC) $(ASOUND_LIBS) -x c - -o /dev/null 2>/dev/null && echo yes)

all: $(LED_LIB) $(SCR_LIB) $(CTL_BIN) $(MAIN_BIN) $(SCREEN_BINS) $(QCTL_BIN)
ifneq ($(ASOUND_AVAIL),yes)
	@echo "==> -lasound not available for target, skipping $(VOL_BIN)"
else
	@$(MAKE) $(VOL_BIN)
endif

# LED ring library
$(LED_LIB): $(LED_LIB_OBJ)
	ar rcs $@ $^

atri_led.o: src/atri_led.c include/atri_led.h
	$(CC) $(CFLAGS) -c $< -o $@

atri_led_anim.o: src/atri_led_anim.c include/atri_led.h include/atri_led_anim.h
	$(CC) $(CFLAGS) -c $< -o $@

# SPI screen library
$(SCR_LIB): $(SCR_LIB_OBJ)
	ar rcs $@ $^

quasar_screen.o: src/quasar_screen.c include/quasar_screen.h
	$(CC) $(CFLAGS) -c $< -o $@

# LED ring binaries
$(CTL_BIN): src/atri_led_ctl.c $(LED_LIB) include/atri_led.h
	$(CC) $(CFLAGS) $< -L. -latri_led $(LDFLAGS) -o $@

$(MAIN_BIN): src/atriled.c $(LED_LIB) include/atri_led.h include/atri_led_anim.h
	$(CC) $(CFLAGS) -I./include $< -L. -latri_led $(LDFLAGS) -o $@

$(VOL_BIN): src/atrivolume.c $(LED_LIB) include/atri_led.h
	$(CC) $(CFLAGS) $< -L. -latri_led $(ASOUND_LIBS) $(LDFLAGS) -o $@

$(SCR_BIN): src/atriled_screen.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# SPI screen binaries
$(QTST_BIN): src/quasar_led_test.c $(SCR_LIB)
	$(CC) $(CFLAGS) $< -L. -lquasar_screen $(LDFLAGS) -o $@

$(QTXT_BIN): src/quasar_led_text.c $(SCR_LIB)
	$(CC) $(CFLAGS) $< -L. -lquasar_screen $(LDFLAGS) -o $@

$(QDEMO_BIN): src/quasar_led_demo.c $(SCR_LIB)
	$(CC) $(CFLAGS) $< -L. -lquasar_screen $(LDFLAGS) -o $@

$(QINFO_BIN): src/quasar_led_info.c $(SCR_LIB)
	$(CC) $(CFLAGS) $< -L. -lquasar_screen $(LDFLAGS) -o $@

$(QCTL_BIN): src/quasar_led_ctl.c $(LED_LIB) include/atri_led.h include/atri_led_anim.h
	$(CC) $(CFLAGS) -I./include $< -L. -latri_led $(LDFLAGS) -o $@

install: all
	mkdir -p $(DESTDIR)/usr/lib
	mkdir -p $(DESTDIR)/usr/bin
	mkdir -p $(DESTDIR)/usr/include
	mkdir -p $(DESTDIR)/lib/systemd/system
	mkdir -p $(DESTDIR)/etc/atriled/animations
	cp $(LED_LIB) $(DESTDIR)/usr/lib/
	cp $(SCR_LIB) $(DESTDIR)/usr/lib/
	cp $(CTL_BIN) $(DESTDIR)/usr/bin/
	cp $(MAIN_BIN) $(DESTDIR)/usr/bin/
	cp $(SCR_BIN) $(DESTDIR)/usr/bin/
	cp $(SCREEN_BINS) $(DESTDIR)/usr/bin/
	cp $(QCTL_BIN) $(DESTDIR)/usr/bin/
ifneq ($(ASOUND_AVAIL),yes)
	@echo "==> skipping atrivolume install (not built)"
else
	cp $(VOL_BIN) $(DESTDIR)/usr/bin/
endif
	cp include/atri_led.h $(DESTDIR)/usr/include/
	cp include/atri_led_anim.h $(DESTDIR)/usr/include/
	cp include/quasar_screen.h $(DESTDIR)/usr/include/
	cp etc/atriled/animations/*.led $(DESTDIR)/etc/atriled/animations/
	cp etc/atriled/animations/*.anim $(DESTDIR)/etc/atriled/animations/ 2>/dev/null || true
	mkdir -p $(DESTDIR)/lib/firmware
	cp lib/firmware/*.manifest $(DESTDIR)/lib/firmware/
	cp systemd/*.service $(DESTDIR)/lib/systemd/system/
ifneq ($(ASOUND_AVAIL),yes)
	rm -f $(DESTDIR)/lib/systemd/system/atrivolume.service
endif

clean:
	rm -f *.o $(LED_LIB) $(SCR_LIB) $(ALL_BINS) $(VOL_BIN)

.PHONY: all install clean
