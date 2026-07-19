/* Portability smoke test for the Lawn C library.
 *
 * Builds and runs on any C11 platform with a POSIX clock and <sys/queue.h>
 * (Linux, macOS, the BSDs) with no glibc-only headers, matching the
 * self-contained portability of the wahern timing-wheel code. Exercises the
 * public API from lawn.h; a failed assert exits non-zero.
 *
 *   cc -std=c11 -I src -I src/utils \
 *      src/tests/test_lawn_portable.c src/lawn.c src/utils/hashmap.c \
 *      src/utils/hash_funcs.c src/utils/millisecond_time.c -lm \
 *      -o test_lawn_portable && ./test_lawn_portable
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "lawn.h"

int main(void) {
    Lawn *l = newLawn();
    assert(l != NULL);

    char key[32];
    for (int i = 0; i < 100; i++) {
        snprintf(key, sizeof key, "k%d", i);
        /* 10 distinct TTLs (1000..1009) across 100 keys */
        int rc = set_element_ttl(l, key, strlen(key), (mstime_t)(1000 + i % 10));
        assert(rc == LAWN_OK);
    }
    assert(timer_count(l) == 100);
    assert(ttl_count(l) == 10);

    snprintf(key, sizeof key, "k%d", 42);
    assert(get_element_exp(l, key) > 0);
    assert(del_element_exp(l, key) == LAWN_OK);
    assert(timer_count(l) == 99);
    assert(next_at(l) > 0);

    freeLawn(l);
    printf("lawn portable smoke test: OK\n");
    return 0;
}
