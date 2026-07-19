# C timer-store benchmark suite (macOS/clang; portable C11).
CC      = cc
CFLAGS  = -O2 -std=c11 -Wall -pthread \
          -I. -I../.. -I../../utils -I../../../article/src/c/wheel
LDFLAGS = -lm

HARNESS  = util.c registry.c clock.c
ADAPTERS = impl/lawn.c impl/lawn2.c impl/wahern.c impl/naive.c
DEPS     = ../../lawn.c ../../utils/hashmap.c ../../utils/hash_funcs.c \
           ../../../article/src/c/wheel/timeout.c ../../lawn2.c

all: test bench

test: test.c $(HARNESS) $(ADAPTERS) $(DEPS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

bench: bench.c $(HARNESS) $(ADAPTERS) $(DEPS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f test bench

.PHONY: all clean
