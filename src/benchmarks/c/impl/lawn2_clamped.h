#ifndef LAWN2_CLAMPED_H
#define LAWN2_CLAMPED_H

#include <stdint.h>

/* Rounds ttl up to its Linux-kernel timer-wheel bucket boundary. Exposed so
 * test.c can compute expected fire ticks independently of the adapter. */
uint64_t clamp_timer_ttl(uint64_t ttl);

#endif /* LAWN2_CLAMPED_H */
