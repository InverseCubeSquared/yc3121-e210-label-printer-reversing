# rmicron C headers: header-only, nothing to build for users.
# make test    builds and runs the offline tests

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -pedantic -O2

test: test-proto
	./test-proto

test-proto: test-proto.c rmicron-proto.h rmicron-usb.h rmicron-bluetooth.h
	$(CC) $(CFLAGS) -o $@ test-proto.c

clean:
	rm -f test-proto

.PHONY: test clean
