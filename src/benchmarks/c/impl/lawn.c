/* Adapter over the repo's Lawn (src/lawn.c), driven by the injected logical
 * clock in clock.c: g_cts_now is set to the store's tick before each Lawn
 * call, so Lawn's internal current_time_ms() sees deterministic ticks. Keys are
 * decimal id strings (Lawn's del/get take a NUL-terminated key). */
#include "cts.h"
#include "lawn.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct cts_store {
    Lawn *l;
    uint64_t now;
};

/* Fast u64 -> decimal string (snprintf is ~50-100ns and would dominate the
 * per-op cost). Returns length; buf is NUL-terminated. Lawn is string-keyed by
 * design, so a string key is inherent; only the slow formatter is avoided. */
static size_t keyof(uint64_t id, char *buf) {
    char tmp[24];
    int n = 0;
    if (id == 0) tmp[n++] = '0';
    while (id) { tmp[n++] = (char)('0' + id % 10); id /= 10; }
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return (size_t)n;
}

static cts_store *lawn_create(void) {
    struct cts_store *s = calloc(1, sizeof *s);
    s->l = newLawn();
    return s;
}

static void lawn_destroy(cts_store *s) { freeLawn(s->l); free(s); }

static void lawn_start(cts_store *s, uint64_t id, uint64_t ttl) {
    g_cts_now = s->now;
    char k[24]; size_t len = keyof(id, k);
    set_element_ttl(s->l, k, len, (mstime_t)ttl);
}

static int lawn_stop(cts_store *s, uint64_t id) {
    g_cts_now = s->now;
    char k[24]; keyof(id, k);
    if (get_element_exp(s->l, k) == (mstime_t)-1) return 0;
    del_element_exp(s->l, k);
    return 1;
}

static uint64_t lawn_tick(cts_store *s) {
    s->now++;
    g_cts_now = s->now;
    ElementQueue *q = pop_expired(s->l);
    uint64_t n = q ? (uint64_t)q->len : 0;
    if (q) freeQueue(q);
    return n;
}

static uint64_t lawn_size(cts_store *s) { return (uint64_t)timer_count(s->l); }

const cts_vtable cts_lawn_vtable = {
    "lawn", lawn_create, lawn_destroy,
    lawn_start, lawn_stop, lawn_tick, lawn_size,
};
