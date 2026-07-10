# SPDX-License-Identifier: GPL-2.0

PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib
SYSCONFDIR ?= /etc
SHAREDIR ?= $(PREFIX)/share
UNITDIR ?= /lib/systemd/system

TARGET = atri-main
SRCS = src/main.c src/log.c src/config.c src/screen.c src/ring.c src/animator.c src/input.c
OBJS = $(SRCS:.c=.o)

CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += -D_GNU_SOURCE

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

install: all
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -Dm644 config/main.conf $(DESTDIR)$(SYSCONFDIR)/atri-main/main.conf
	install -Dm644 systemd/atri-main.service $(DESTDIR)$(UNITDIR)/atri-main.service
	install -dm755 $(DESTDIR)$(SHAREDIR)/atri-main/animations/screen_raw
	install -dm755 $(DESTDIR)$(SHAREDIR)/atri-main/animations/ring
	cp -r config/animations/screen_raw/* $(DESTDIR)$(SHAREDIR)/atri-main/animations/screen_raw/
	cp -r config/animations/ring/* $(DESTDIR)$(SHAREDIR)/atri-main/animations/ring/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -rf $(DESTDIR)$(SYSCONFDIR)/atri-main
	rm -f $(DESTDIR)$(UNITDIR)/atri-main.service
	rm -rf $(DESTDIR)$(SHAREDIR)/atri-main
