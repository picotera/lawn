# Multithreaded concurrency benchmark (real pthreads, wall-clock).
CC      = cc
CFLAGS  = -O2 -std=c11 -Wall -pthread \
          -I.. -I../../.. -I../../../utils -I../../../../article/src/c/wheel
LDFLAGS = -lm -pthread

SRC = concurrent.c ../util.c ../registry.c ../clock.c \
      ../impl/lawn.c ../impl/lawn2.c ../impl/wahern.c ../impl/naive.c \
      ../../../lawn.c ../../../lawn2.c ../../../utils/hashmap.c ../../../utils/hash_funcs.c \
      ../../../../article/src/c/wheel/timeout.c

concurrent: $(SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f concurrent

.PHONY: clean
