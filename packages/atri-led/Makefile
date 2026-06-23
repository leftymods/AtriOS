CC ?= gcc
CFLAGS = -O2 -Wall -I./include -D_GNU_SOURCE
LDFLAGS = -lm
ASOUND_LIBS = -lasound

LIB_SRC = src/atri_led.c src/atri_led_anim.c
LIB_OBJ = atri_led.o atri_led_anim.o
LIB = libatri_led.a
CTL_BIN = atrledctl
MAIN_BIN = atrled
VOL_BIN = atrivolume

# Check if -lasound is available for target (cross-compile may lack it)
ASOUND_AVAIL := $(shell echo 'int main(){return 0;}' | $(CC) $(ASOUND_LIBS) -x c - -o /dev/null 2>/dev/null && echo yes)

all: $(LIB) $(CTL_BIN) $(MAIN_BIN)
ifneq ($(ASOUND_AVAIL),yes)
	@echo "==> -lasound not available for target, skipping $(VOL_BIN)"
else
	@$(MAKE) $(VOL_BIN)
endif

$(LIB): $(LIB_OBJ)
	ar rcs $@ $^

atri_led.o: src/atri_led.c include/atri_led.h
	$(CC) $(CFLAGS) -c $< -o $@

atri_led_anim.o: src/atri_led_anim.c include/atri_led.h include/atri_led_anim.h
	$(CC) $(CFLAGS) -c $< -o $@

$(CTL_BIN): src/atri_led_ctl.c $(LIB) include/atri_led.h
	$(CC) $(CFLAGS) $< -L. -latri_led $(LDFLAGS) -o $@

$(MAIN_BIN): src/atriled.c $(LIB) include/atri_led.h include/atri_led_anim.h
	$(CC) $(CFLAGS) -I./include $< -L. -latri_led $(LDFLAGS) -o $@

$(VOL_BIN): src/atrivolume.c $(LIB) include/atri_led.h
	$(CC) $(CFLAGS) $< -L. -latri_led $(ASOUND_LIBS) $(LDFLAGS) -o $@

install: $(LIB) $(CTL_BIN) $(MAIN_BIN)
	mkdir -p $(DESTDIR)/usr/lib
	mkdir -p $(DESTDIR)/usr/bin
	mkdir -p $(DESTDIR)/usr/include
	mkdir -p $(DESTDIR)/lib/systemd/system
	cp $(LIB) $(DESTDIR)/usr/lib/
	cp $(CTL_BIN) $(DESTDIR)/usr/bin/
	cp $(MAIN_BIN) $(DESTDIR)/usr/bin/
	cp include/atri_led.h $(DESTDIR)/usr/include/
	cp include/atri_led_anim.h $(DESTDIR)/usr/include/
ifneq ($(ASOUND_AVAIL),yes)
	@echo "==> skipping atrivolume install (not built)"
else
	cp $(VOL_BIN) $(DESTDIR)/usr/bin/
	cp systemd/atrivolume.service $(DESTDIR)/lib/systemd/system/
endif

clean:
	rm -f *.o $(LIB) $(CTL_BIN) $(MAIN_BIN) $(VOL_BIN)

.PHONY: all install clean
