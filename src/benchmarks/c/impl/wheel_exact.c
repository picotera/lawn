/* An exact hierarchical timer wheel with lazy cascading: timers are placed
 * once, directly, into the (level, slot) their remaining delta belongs to,
 * and a level's slot is only redistributed ("cascaded") the moment that
 * level's own granularity boundary is crossed. This preserves exact
 * expiry -- unlike the real Linux 4.8+ kernel redesign, whose defining move
 * is the opposite trade: it eliminates cascades entirely by rounding
 * expiry *up* to the level's granularity instead (see calc_index in
 * article/src/c/wheel/kernel_impls/linux_kern_timer.c). This is an
 * independent implementation of the published cascading-wheel algorithm
 * (Varghese & Lauck, TW/TW87 in the bib), not a Linux-kernel model and not
 * a literal port of anything.
 * 8 levels x 64 slots/level (6 bits/level). Slots are dynamic id arrays
 * with id-indexed metadata for O(1) swap-removal, mirroring
 * impl/naive.c's pattern. */
#include "cts.h"
#include <stdlib.h>

#define LVL_BITS   6
#define LVL_SIZE   (1u << LVL_BITS)      /* 64 slots per level */
#define LVL_MASK   (LVL_SIZE - 1)
#define NUM_LEVELS 8
#define LVL_GRAN(n) (((uint64_t)1) << (LVL_BITS * (n)))

typedef struct {
    uint64_t *ids;
    size_t    len, cap;
} slot_t;

struct cts_store {
    uint64_t  now;
    slot_t    levels[NUM_LEVELS][LVL_SIZE];
    /* id-indexed metadata (grows with max id seen) */
    uint64_t *id_expire;
    int8_t   *id_lvl;    /* -1 if absent */
    uint32_t *id_idx;    /* slot index within its level */
    size_t   *id_pos;    /* position within that slot's ids[] */
    size_t    id_cap;
    uint64_t  live;
};

static cts_store *we_create(void) {
    return calloc(1, sizeof(struct cts_store));
}

static void we_destroy(cts_store *s) {
    for (int l = 0; l < NUM_LEVELS; l++)
        for (unsigned i = 0; i < LVL_SIZE; i++)
            free(s->levels[l][i].ids);
    free(s->id_expire); free(s->id_lvl); free(s->id_idx); free(s->id_pos);
    free(s);
}

static void ensure_id_cap(struct cts_store *s, uint64_t id) {
    if (id < s->id_cap) return;
    size_t nc = s->id_cap ? s->id_cap : 1024;
    while (id >= nc) nc <<= 1;
    s->id_expire = realloc(s->id_expire, nc * sizeof *s->id_expire);
    s->id_lvl    = realloc(s->id_lvl,    nc * sizeof *s->id_lvl);
    s->id_idx    = realloc(s->id_idx,    nc * sizeof *s->id_idx);
    s->id_pos    = realloc(s->id_pos,    nc * sizeof *s->id_pos);
    for (size_t i = s->id_cap; i < nc; i++) s->id_lvl[i] = -1;
    s->id_cap = nc;
}

static void slot_push(slot_t *sl, uint64_t id, size_t *pos_out) {
    if (sl->len == sl->cap) {
        sl->cap = sl->cap ? sl->cap * 2 : 4;
        sl->ids = realloc(sl->ids, sl->cap * sizeof *sl->ids);
    }
    *pos_out = sl->len;
    sl->ids[sl->len++] = id;
}

static void slot_remove_at(slot_t *sl, size_t pos, struct cts_store *s) {
    size_t last = sl->len - 1;
    if (pos != last) {
        uint64_t moved = sl->ids[last];
        sl->ids[pos] = moved;
        s->id_pos[moved] = pos;
    }
    sl->len--;
}

/* Places id (whose id_expire[] is already set) into the level/slot its
 * remaining delta (expire - now) belongs to. Used for fresh inserts and
 * for cascade re-placement, where `now` has already moved on. */
static void place(struct cts_store *s, uint64_t id) {
    uint64_t expire = s->id_expire[id];
    uint64_t delta = expire > s->now ? expire - s->now : 0;
    int level = 0;
    while (level < NUM_LEVELS - 1 && delta >= LVL_GRAN(level + 1)) level++;
    uint32_t idx = (uint32_t)((expire >> (LVL_BITS * level)) & LVL_MASK);
    size_t pos;
    slot_push(&s->levels[level][idx], id, &pos);
    s->id_lvl[id] = (int8_t)level;
    s->id_idx[id] = idx;
    s->id_pos[id] = pos;
}

static void we_start(cts_store *s, uint64_t id, uint64_t ttl) {
    ensure_id_cap(s, id);
    s->id_expire[id] = s->now + ttl;
    place(s, id);
    s->live++;
}

static int we_stop(cts_store *s, uint64_t id) {
    if (id >= s->id_cap || s->id_lvl[id] < 0) return 0;
    slot_t *sl = &s->levels[s->id_lvl[id]][s->id_idx[id]];
    slot_remove_at(sl, s->id_pos[id], s);
    s->id_lvl[id] = -1;
    s->live--;
    return 1;
}

static uint64_t we_tick(cts_store *s) {
    s->now++;
    /* Cascade only the levels whose own granularity boundary was just
     * crossed, breaking at the first level that hasn't wrapped yet. */
    for (int lvl = 1; lvl < NUM_LEVELS; lvl++) {
        uint64_t gran = LVL_GRAN(lvl);
        if (s->now & (gran - 1)) break;
        uint32_t idx = (uint32_t)((s->now >> (LVL_BITS * lvl)) & LVL_MASK);
        slot_t *sl = &s->levels[lvl][idx];
        while (sl->len > 0) {
            uint64_t id = sl->ids[sl->len - 1];
            slot_remove_at(sl, sl->len - 1, s);
            place(s, id);
        }
    }
    uint32_t idx0 = (uint32_t)(s->now & LVL_MASK);
    slot_t *sl0 = &s->levels[0][idx0];
    uint64_t fired = sl0->len;
    for (size_t j = 0; j < sl0->len; j++) s->id_lvl[sl0->ids[j]] = -1;
    sl0->len = 0;
    s->live -= fired;
    return fired;
}

static uint64_t we_size(cts_store *s) { return s->live; }

/* Jump the clock forward to target by running the already-correct we_tick
 * once per elapsed tick, so every cascade boundary the jump crosses is
 * processed exactly as it would be under tick-by-tick advancement. Costs
 * O(target - now) rather than O(levels), but advance() is only ever called
 * from pre_lifecycle's untimed preload setup (benchmark.c), never from a
 * timed payload, so this has no effect on any reported latency. Anything
 * that fires during the jump (shouldn't happen under the staggered-preload
 * precondition that target stays below every live deadline, but handled
 * defensively) is drained and counted down in live via we_tick's own
 * bookkeeping, mirroring wahern_advance's same defensive drain. */
static void we_advance(cts_store *s, uint64_t target) {
    while (s->now < target) we_tick(s);
}

const cts_vtable cts_wheel_exact_vtable = {
    "wheelexact", we_create, we_destroy,
    we_start, we_stop, we_tick, we_size, we_advance,
};
