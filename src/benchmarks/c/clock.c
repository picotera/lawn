/* Injected logical clock. Replaces src/utils/millisecond_time.c so lawn.c's
 * internal current_time_ms() reads our deterministic tick counter instead of
 * wall time. The lawn adapter sets g_cts_now before each lawn call. */
#include "millisecond_time.h"
#include <stdint.h>

uint64_t g_cts_now = 0;

mstime_t current_time_ms(void) {
    return (mstime_t)g_cts_now;
}
