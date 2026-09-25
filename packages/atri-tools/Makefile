CC ?= gcc
CFLAGS = -O2 -Wall -D_GNU_SOURCE
LDFLAGS = -static -lm

ATRI_CLI_BIN = atri
TUI_BIN = atri-tui
HWPROBE_BIN = atri-hwprobe
AUTOBRIGHTNESS_BIN = atri-autobrightness

ALL_BINS = $(ATRI_CLI_BIN) $(TUI_BIN) $(HWPROBE_BIN) $(AUTOBRIGHTNESS_BIN)

all: $(ALL_BINS)

$(ATRI_CLI_BIN): src/atri_cli.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

$(TUI_BIN): src/atri_tui.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

$(HWPROBE_BIN): src/atri_hwprobe.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

$(AUTOBRIGHTNESS_BIN): src/atri_autobrightness.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

install: all
	mkdir -p $(DESTDIR)/usr/bin
	mkdir -p $(DESTDIR)/lib/systemd/system
	mkdir -p $(DESTDIR)/etc/atri
	cp $(ATRI_CLI_BIN) $(DESTDIR)/usr/bin/
	cp $(TUI_BIN) $(DESTDIR)/usr/bin/
	cp $(HWPROBE_BIN) $(DESTDIR)/usr/bin/
	cp $(AUTOBRIGHTNESS_BIN) $(DESTDIR)/usr/bin/
	# Backward-compatible and canonical symlinks
	ln -sf $(TUI_BIN) $(DESTDIR)/usr/bin/atri-setup
	ln -sf $(TUI_BIN) $(DESTDIR)/usr/bin/atri-menu
	ln -sf $(HWPROBE_BIN) $(DESTDIR)/usr/bin/atri-pcba
	ln -sf $(HWPROBE_BIN) $(DESTDIR)/usr/bin/atri-als
	ln -sf $(HWPROBE_BIN) $(DESTDIR)/usr/bin/atri-buttons
	ln -sf $(HWPROBE_BIN) $(DESTDIR)/usr/bin/atri-knob
	cp etc/autobrightness.conf $(DESTDIR)/etc/atri/ 2>/dev/null || true
	cp systemd/*.service $(DESTDIR)/lib/systemd/system/ 2>/dev/null || true

clean:
	rm -f $(ALL_BINS)
