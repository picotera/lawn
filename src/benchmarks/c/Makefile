# C timer-store benchmark suite (macOS/clang; portable C11).
CC      = cc
CFLAGS  = -O2 -std=c11 -Wall -pthread \
          -I. -I../.. -I../../utils -I../../../article/src/c/wheel \
          -DWHEEL_NUM=6
LDFLAGS = -lm

HARNESS  = util.c
ADAPTERS = impl/lawn.c impl/lawn2.c impl/wahern.c impl/naive.c impl/heap.c impl/linuxwheel.c
DEPS     = ../../lawn.c ../../utils/hashmap.c \
           ../../../article/src/c/wheel/timeout.c ../../lawn2.c

all: test bench

test: test.c $(HARNESS) $(ADAPTERS) $(DEPS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

bench: bench.c $(HARNESS) $(ADAPTERS) $(DEPS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

run:
	./test
	./bench

clean:
	rm -f test bench

.PHONY: all clean
