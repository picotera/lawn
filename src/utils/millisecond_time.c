#define _POSIX_C_SOURCE 199309L
#include "millisecond_time.h"
#include <time.h>

/*
 * @return current time in milliseconds
 */
mstime_t current_time_ms(void) {
  struct timespec spec;
  clock_gettime(CLOCK_REALTIME, &spec);
  return (mstime_t)spec.tv_sec * 1000 + (mstime_t)(spec.tv_nsec / 1000000);
}
