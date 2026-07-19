/* Adapter over William Ahern's tickless hierarchical timing wheel (timeout.c).
 * Logical clock: tick() advances curtime via timeouts_update. Each timer is an
 * individually malloc'd struct timeout (stable address, required: the wheel
 * links them into TAILQ lists); a growable id->timeout* table gives O(1)
 * delete. The id rides in callback.arg so tick-drain can recover it. */
#include "cts.h"
#include "timeout.h"
#include <stdlib.h>
#include <stdint.h>

struct cts_store {
    struct timeouts *T;
    struct timeout **timers;   /* id -> timeout*, NULL if absent */
    size_t cap;
    uint64_t now;
    uint64_t live;
};

static cts_store *wahern_create(void) {
    struct cts_store *s = calloc(1, sizeof *s);
    int err = 0;
    s->T = timeouts_open(TIMEOUT_mHZ, &err);
    return s;
}

static void wahern_destroy(cts_store *s) {
    for (size_t i = 0; i < s->cap; i++) {
        if (s->timers[i]) { timeouts_del(s->T, s->timers[i]); free(s->timers[i]); }
    }
    free(s->timers);
    timeouts_close(s->T);
    free(s);
}

static void ensure_cap(struct cts_store *s, uint64_t id) {
    if (id < s->cap) return;
    size_t nc = s->cap ? s->cap : 1024;
    while (id >= nc) nc <<= 1;
    s->timers = realloc(s->timers, nc * sizeof *s->timers);
    for (size_t i = s->cap; i < nc; i++) s->timers[i] = NULL;
    s->cap = nc;
}

static void wahern_start(cts_store *s, uint64_t id, uint64_t ttl) {
    ensure_cap(s, id);
    struct timeout *to = malloc(sizeof *to);
    timeout_init(to, 0);
    /* set callback fields directly: the timeout_setcb macro's param names
     * (fn/arg) collide with the struct member names and miscompile. */
    to->callback.fn = 0;
    to->callback.arg = (void *)(uintptr_t)id;
    s->timers[id] = to;
    timeouts_add(s->T, to, ttl);   /* relative to curtime == now */
    s->live++;
}

static int wahern_stop(cts_store *s, uint64_t id) {
    if (id >= s->cap || !s->timers[id]) return 0;
    timeouts_del(s->T, s->timers[id]);
    free(s->timers[id]);
    s->timers[id] = NULL;
    s->live--;
    return 1;
}

static uint64_t wahern_tick(cts_store *s) {
    s->now++;
    timeouts_update(s->T, s->now);
    uint64_t fired = 0;
    struct timeout *to;
    while ((to = timeouts_get(s->T))) {
        uint64_t id = (uint64_t)(uintptr_t)to->callback.arg;
        free(to);
        s->timers[id] = NULL;
        fired++;
    }
    s->live -= fired;
    return fired;
}

static uint64_t wahern_size(cts_store *s) { return s->live; }

const cts_vtable cts_wahern_vtable = {
    "wahern", wahern_create, wahern_destroy,
    wahern_start, wahern_stop, wahern_tick, wahern_size,
};
