CC ?= gcc
CFLAGS = -O2 -Wall -D_GNU_SOURCE
LDFLAGS = -static

ONBOARD_BIN = atri-onboard
WIFI_DIAG_BIN = atri-wifi-diag
WIFI_INIT_BIN = atri-wireless-init
ZGBIN = atri-zigbee

ALL_BINS = $(ONBOARD_BIN) $(WIFI_DIAG_BIN) $(WIFI_INIT_BIN) $(ZGBIN)

all: $(ALL_BINS)

$(ONBOARD_BIN): src/atri_onboard.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

$(WIFI_DIAG_BIN): src/atri_wifi_diag.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

$(WIFI_INIT_BIN): src/atri_wireless_init.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

$(ZGBIN): src/atri_zigbee.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

install: all
	mkdir -p $(DESTDIR)/usr/bin
	mkdir -p $(DESTDIR)/lib/systemd/system
	mkdir -p $(DESTDIR)/etc/udev/rules.d
	mkdir -p $(DESTDIR)/lib/firmware
	cp $(ONBOARD_BIN) $(DESTDIR)/usr/bin/
	cp $(WIFI_DIAG_BIN) $(DESTDIR)/usr/bin/
	cp $(WIFI_INIT_BIN) $(DESTDIR)/usr/bin/
	cp $(ZGBIN) $(DESTDIR)/usr/bin/
	ln -sf $(ONBOARD_BIN) $(DESTDIR)/usr/bin/atri-phone-setup
	ln -sf $(WIFI_DIAG_BIN) $(DESTDIR)/usr/bin/atri-wireless
	cp systemd/*.service $(DESTDIR)/lib/systemd/system/ 2>/dev/null || true
	cp -r etc/udev/rules.d/* $(DESTDIR)/etc/udev/rules.d/ 2>/dev/null || true
	cp -r lib/firmware/* $(DESTDIR)/lib/firmware/ 2>/dev/null || true

clean:
	rm -f $(ALL_BINS)
