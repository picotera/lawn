/* cts adapter for lawn2 with Linux-kernel-style timer-wheel TTL clamping.
 * Every start() rounds ttl up to its wheel level's bucket boundary before
 * inserting, so far-out timers lose precision the same way the kernel's
 * legacy timer wheel does. Node storage matches impl/lawn2.c. */
#include "cts.h"
#include "lawn2.h"
#include "lawn2_clamped.h"
#include <stdlib.h>

#define LVL_BITS      6  /* 64 buckets per level (2^6) */
#define LVL_CLK_SHIFT 3  /* resolution drops by factor of 8 per level (2^3) */
#define LVL_DEPTH     9  /* levels 0 through 8 */

uint64_t clamp_timer_ttl(uint64_t ttl) {
    /* Level 0: 0 to 63 ticks. Exact 1-tick precision, no clamping. */
    if (ttl < (1ULL << LVL_BITS)) {
        return ttl;
    }

    /* Levels 1 through 7. */
    for (int level = 1; level < LVL_DEPTH - 1; level++) {
        unsigned int shift = level * LVL_CLK_SHIFT;
        uint64_t level_max = 1ULL << (LVL_BITS + shift);

        if (ttl < level_max) {
            uint64_t mask = (1ULL << shift) - 1;
            /* Round up so the timer never fires early, then mask off the
             * lower bits to align with the bucket boundary. */
            return (ttl + mask) & ~mask;
        }
    }

    /* Level 8: catch-all beyond the standard wheel thresholds. */
    unsigned int max_shift = (LVL_DEPTH - 1) * LVL_CLK_SHIFT;
    uint64_t max_mask = (1ULL << max_shift) - 1;
    return (ttl + max_mask) & ~max_mask;
}

struct cts_store {
    lawn2       *l;
    timer_store *st;
};

static cts_store *l2c_create(void) {
    struct cts_store *s = calloc(1, sizeof *s);
    s->l = lawn2_new();
    s->st = init_store();
    return s;
}

static void l2c_destroy(cts_store *s) {
    lawn2_free(s->l);
    destroy_store(s->st);
    free(s);
}

static void l2c_start(cts_store *s, uint64_t id, uint64_t ttl) {
    lawn2_add(s->l, timer_for(s->st, id), clamp_timer_ttl(ttl));
}

static int l2c_stop(cts_store *s, uint64_t id) {
    lawn2_timer *n = timer_for(s->st, id);
    if (!n->in_store) return 0;
    lawn2_del(s->l, n);
    return 1;
}

static uint64_t l2c_tick(cts_store *s) {
    lawn2_timer *expired_head = NULL;
    uint64_t count = lawn2_tick(s->l, &expired_head);
    return count;
}
static uint64_t l2c_size(cts_store *s) { return lawn2_size(s->l); }

/* Jump the clock forward with no expiry processing (staggered preload keeps
 * every deadline in the future, so nothing is due). Mirrors impl/lawn2.c. */
static void l2c_advance(cts_store *s, uint64_t target) { lawn2_set_now(s->l, target); }

const cts_vtable cts_lawn2_clamped_vtable = {
    "lawn2clamp", l2c_create, l2c_destroy,
    l2c_start, l2c_stop, l2c_tick, l2c_size, l2c_advance,
};
