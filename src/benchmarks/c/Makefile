# C timer-store benchmark suite (macOS/clang; portable C11).
CC      = cc
CFLAGS  = -O2 -std=c11 -Wall -Wextra -pthread \
          -I. -I../.. -I../../utils -I../../../article/src/c/wheel \
          -DWHEEL_NUM=6
LDFLAGS = -lm

HARNESS  = util.c
ADAPTERS = impl/lawn.c impl/lawn2.c impl/lawn2_clamped.c impl/wahern.c impl/naive.c impl/heap.c impl/wheel_exact.c
DEPS     = ../../lawn.c ../../utils/hashmap.c \
           ../../../article/src/c/wheel/timeout.c ../../lawn2.c

all: test benchmark

test: test.c $(HARNESS) $(ADAPTERS) $(DEPS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

benchmark: benchmark.c $(HARNESS) $(ADAPTERS) $(DEPS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

run:
	./test
	./benchmark

clean:
	rm -f test benchmark

.PHONY: all clean
