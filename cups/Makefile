# CUPS driver for the rmicro E210 label printer.
#
#   make                       build filter and Bluetooth backend
#   sudo make install          install filter, backend and PPDs into CUPS
#   make setup usb             add a print queue for the printer on USB
#   make setup bluetooth       add a print queue over Bluetooth
#                              (ADDR=AA:BB:CC:DD:EE:FF, default: scan for "E210")
#   make remove usb|bluetooth  delete that queue
#   sudo make uninstall        remove the installed files
#
# Build needs libcups2-dev and libcupsimage2-dev. "setup" needs lpadmin
# rights (root or the lpadmin group).

CC       ?= cc
CFLAGS   ?= -std=c99 -Wall -Wextra -O2
CPPFLAGS += -I../c
CUPS_CFLAGS := $(shell cups-config --cflags)
CUPS_LIBS   := $(shell cups-config --image --libs)
SERVERBIN   := $(shell cups-config --serverbin)
DATADIR     := $(shell cups-config --datadir)
MODELDIR    := $(DATADIR)/model/rmicron
DESTDIR     ?=

QUEUE_USB := rmicron-e210-usb
QUEUE_BT  := rmicron-e210-bluetooth
ADDR      ?=

FILTER  := rastertormicron
BACKEND := rmicron-bt
HEADERS := ../c/rmicron-proto.h ../c/rmicron-bluetooth.h

all: $(FILTER) $(BACKEND)

$(FILTER): rastertormicron.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CUPS_CFLAGS) -o $@ $< $(CUPS_LIBS)

$(BACKEND): rmicron-bt.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

install: all
	install -d $(DESTDIR)$(SERVERBIN)/filter $(DESTDIR)$(SERVERBIN)/backend $(DESTDIR)$(MODELDIR)
	install -m 0755 $(FILTER) $(DESTDIR)$(SERVERBIN)/filter/$(FILTER)
	install -m 0755 $(BACKEND) $(DESTDIR)$(SERVERBIN)/backend/$(BACKEND)
	install -m 0644 rmicron-e210-usb.ppd rmicron-e210-bluetooth.ppd $(DESTDIR)$(MODELDIR)/
	@echo "installed; now run: make setup usb   or   make setup bluetooth"

uninstall:
	rm -f $(DESTDIR)$(SERVERBIN)/filter/$(FILTER) $(DESTDIR)$(SERVERBIN)/backend/$(BACKEND)
	rm -f $(DESTDIR)$(MODELDIR)/rmicron-e210-usb.ppd $(DESTDIR)$(MODELDIR)/rmicron-e210-bluetooth.ppd
	-rmdir $(DESTDIR)$(MODELDIR)

# "make setup usb" / "make setup bluetooth" / "make remove usb": the first
# word selects the action, the second the interface.
ACTION := $(firstword $(filter setup remove,$(MAKECMDGOALS)))

setup remove:
	@[ -n "$(filter usb bluetooth,$(MAKECMDGOALS))" ] || \
	    { echo "usage: make $@ usb | make $@ bluetooth [ADDR=AA:BB:CC:DD:EE:FF]"; exit 1; }

usb:
ifeq ($(ACTION),setup)
	@uri=$$(lpinfo --include-schemes usb -v 2>/dev/null | awk '/usb:\/\/YICHIP\// {print $$2; exit}'); \
	[ -n "$$uri" ] || { echo "E210 not found on USB (lpinfo -v); is it plugged in and on?"; exit 1; }; \
	echo "adding $(QUEUE_USB) -> $$uri"; \
	lpadmin -p $(QUEUE_USB) -E -v "$$uri" -m rmicron/rmicron-e210-usb.ppd \
	    -D "E210 label printer (USB)" -o printer-is-shared=false && \
	echo "done; test with: lp -d $(QUEUE_USB) -o media=w85h57 file.pdf"
else ifeq ($(ACTION),remove)
	lpadmin -x $(QUEUE_USB)
else
	@echo "usage: make setup usb | make remove usb"; exit 1
endif

bluetooth:
ifeq ($(ACTION),setup)
	@addr="$(ADDR)"; \
	e210addr() { bluetoothctl devices 2>/dev/null | awk '$$3 ~ /^E2/ {print $$2; exit}'; }; \
	if [ -z "$$addr" ]; then addr=$$(e210addr); fi; \
	if [ -z "$$addr" ]; then \
	    echo "scanning for the E210 (10 s)..."; \
	    bluetoothctl --timeout 10 scan on >/dev/null 2>&1; addr=$$(e210addr); \
	fi; \
	[ -n "$$addr" ] || { echo "no E210 known to bluetoothctl; scan first (bluetoothctl --timeout 10 scan on) or pass ADDR=AA:BB:CC:DD:EE:FF"; exit 1; }; \
	echo "adding $(QUEUE_BT) -> rmicron-bt://$$addr"; \
	lpadmin -p $(QUEUE_BT) -E -v "rmicron-bt://$$addr" -m rmicron/rmicron-e210-bluetooth.ppd \
	    -D "E210 label printer (Bluetooth)" -o printer-is-shared=false && \
	echo "done; test with: lp -d $(QUEUE_BT) -o media=w85h57 file.pdf"
else ifeq ($(ACTION),remove)
	lpadmin -x $(QUEUE_BT)
else
	@echo "usage: make setup bluetooth [ADDR=..] | make remove bluetooth"; exit 1
endif

clean:
	rm -f $(FILTER) $(BACKEND)

.PHONY: all install uninstall setup remove usb bluetooth clean
