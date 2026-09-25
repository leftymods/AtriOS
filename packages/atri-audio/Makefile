CC ?= gcc
CFLAGS = -O2 -Wall -I./include -D_GNU_SOURCE
LDFLAGS = -static -lm
ASOUND_LIBS = -lasound

VOL_BIN = atrivolume
SOUND_TEST_BIN = atri-sound-test

ALL_BINS = $(VOL_BIN) $(SOUND_TEST_BIN)

all: $(ALL_BINS)

$(VOL_BIN): src/atrivolume.c src/atri_led.c
	$(CC) $(CFLAGS) $^ $(ASOUND_LIBS) $(LDFLAGS) -o $@

$(SOUND_TEST_BIN): src/atri_sound_test.c
	$(CC) $(CFLAGS) $< $(ASOUND_LIBS) $(LDFLAGS) -o $@

install: all
	mkdir -p $(DESTDIR)/usr/bin
	mkdir -p $(DESTDIR)/lib/systemd/system
	mkdir -p $(DESTDIR)/lib/firmware
	cp $(VOL_BIN) $(DESTDIR)/usr/bin/
	cp $(SOUND_TEST_BIN) $(DESTDIR)/usr/bin/
	ln -sf $(VOL_BIN) $(DESTDIR)/usr/bin/atri-volume
	ln -sf $(SOUND_TEST_BIN) $(DESTDIR)/usr/bin/atrisound-test
	cp systemd/*.service $(DESTDIR)/lib/systemd/system/ 2>/dev/null || true
	cp lib/firmware/* $(DESTDIR)/lib/firmware/ 2>/dev/null || true

clean:
	rm -f $(ALL_BINS)
