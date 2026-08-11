# Multithreaded concurrency benchmark (real pthreads, wall-clock).
CC      = cc
CFLAGS  = -std=c11 -O2 -pthread -I.. -I../../.. -I../../../utils -I../../../../article/src/c/wheel \
          -DWHEEL_NUM=6
LDFLAGS = -lm

SRC = concurrent.c ../util.c \
      ../impl/lawn.c ../impl/lawn2.c ../impl/wahern.c ../impl/naive.c ../impl/heap.c ../impl/wheel_exact.c \
      ../../../lawn.c ../../../utils/hashmap.c \
      ../../../../article/src/c/wheel/timeout.c ../../../lawn2.c

concurrent: $(SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f concurrent

.PHONY: clean
