CC ?= gcc
CFLAGS = -O2 -Wall -I./include -D_GNU_SOURCE
LDFLAGS = -static -lm

# Libraries
SCR_LIB_SRC = src/quasar_screen.c
SCR_LIB_OBJ = quasar_screen.o
SCR_LIB = libquasar_screen.a

# Display Binaries
MATRIX_BIN = atri-matrix
DISP_DAEMON_BIN = atri-displayd
DISP_CLI_BIN = atri-display

ALL_BINS = $(MATRIX_BIN) $(DISP_DAEMON_BIN) $(DISP_CLI_BIN)

all: $(SCR_LIB) $(ALL_BINS)

# Library
$(SCR_LIB): $(SCR_LIB_OBJ)
	ar rcs $@ $^

quasar_screen.o: src/quasar_screen.c include/quasar_screen.h
	$(CC) $(CFLAGS) -c $< -o $@

# Binaries
$(MATRIX_BIN): src/atri_matrix.c $(SCR_LIB) include/quasar_screen.h
	$(CC) $(CFLAGS) -I./include $< -L. -lquasar_screen $(LDFLAGS) -o $@

$(DISP_DAEMON_BIN): src/atri_displayd.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

$(DISP_CLI_BIN): src/atri_display_cli.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

install: all
	mkdir -p $(DESTDIR)/usr/lib
	mkdir -p $(DESTDIR)/usr/bin
	mkdir -p $(DESTDIR)/usr/include
	mkdir -p $(DESTDIR)/lib/systemd/system
	mkdir -p $(DESTDIR)/lib/firmware
	cp $(SCR_LIB) $(DESTDIR)/usr/lib/
	cp $(MATRIX_BIN) $(DESTDIR)/usr/bin/
	cp $(DISP_DAEMON_BIN) $(DESTDIR)/usr/bin/
	cp $(DISP_CLI_BIN) $(DESTDIR)/usr/bin/
	# Backward-compatible symlinks
	ln -sf $(MATRIX_BIN) $(DESTDIR)/usr/bin/atri-screen-test
	ln -sf $(MATRIX_BIN) $(DESTDIR)/usr/bin/atri-screen
	ln -sf $(MATRIX_BIN) $(DESTDIR)/usr/bin/quasar_led_ctl
	ln -sf $(MATRIX_BIN) $(DESTDIR)/usr/bin/quasar_led_test
	ln -sf $(MATRIX_BIN) $(DESTDIR)/usr/bin/quasar_led_demo
	ln -sf $(MATRIX_BIN) $(DESTDIR)/usr/bin/quasar_led_text
	ln -sf $(MATRIX_BIN) $(DESTDIR)/usr/bin/quasar_led_info
	ln -sf $(MATRIX_BIN) $(DESTDIR)/usr/bin/atriled-screen
	cp include/quasar_screen.h $(DESTDIR)/usr/include/
	cp systemd/*.service $(DESTDIR)/lib/systemd/system/ 2>/dev/null || true
	cp lib/firmware/* $(DESTDIR)/lib/firmware/ 2>/dev/null || true

clean:
	rm -f *.o $(SCR_LIB) $(ALL_BINS)
