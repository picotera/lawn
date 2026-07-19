/* Single-level hashed timing wheel (the overflow victim).
 * One flat ring; it must grow to cover the largest live TTL, so memory is
 * O(max TTL span). O(1) insert/delete/tick. Slots are dynamic id arrays with
 * id-indexed metadata for O(1) swap-remove. Ids are dense 0..N-1 in the bench. */
#include "cts.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t *ids;   /* ids parked in this slot */
    size_t    len, cap;
} slot_t;

struct cts_store {
    uint64_t  now;
    size_t    num_slots;      /* power of two */
    slot_t   *slots;
    /* id-indexed metadata (grows with max id seen) */
    uint64_t *id_expire;
    int64_t  *id_slot;        /* -1 if absent */
    size_t   *id_pos;         /* index within its slot's ids[] */
    size_t    id_cap;
    uint64_t  live;
};

static void ensure_id_cap(struct cts_store *s, uint64_t id) {
    if (id < s->id_cap) return;
    size_t nc = s->id_cap ? s->id_cap : 1024;
    while (id >= nc) nc <<= 1;
    s->id_expire = realloc(s->id_expire, nc * sizeof *s->id_expire);
    s->id_slot   = realloc(s->id_slot,   nc * sizeof *s->id_slot);
    s->id_pos    = realloc(s->id_pos,    nc * sizeof *s->id_pos);
    for (size_t i = s->id_cap; i < nc; i++) s->id_slot[i] = -1;
    s->id_cap = nc;
}

static cts_store *naive_create(void) {
    struct cts_store *s = calloc(1, sizeof *s);
    s->num_slots = 256;
    s->slots = calloc(s->num_slots, sizeof(slot_t));
    return s;
}

static void naive_destroy(cts_store *s) {
    for (size_t i = 0; i < s->num_slots; i++) free(s->slots[i].ids);
    free(s->slots);
    free(s->id_expire); free(s->id_slot); free(s->id_pos);
    free(s);
}

static void slot_push(slot_t *sl, uint64_t id, size_t *pos_out) {
    if (sl->len == sl->cap) {
        sl->cap = sl->cap ? sl->cap * 2 : 4;
        sl->ids = realloc(sl->ids, sl->cap * sizeof *sl->ids);
    }
    *pos_out = sl->len;
    sl->ids[sl->len++] = id;
}

static void place(struct cts_store *s, uint64_t id, uint64_t expire) {
    size_t idx = (size_t)(expire & (s->num_slots - 1));
    size_t pos;
    slot_push(&s->slots[idx], id, &pos);
    s->id_expire[id] = expire;
    s->id_slot[id]   = (int64_t)idx;
    s->id_pos[id]    = pos;
}

static void grow_ring(struct cts_store *s, uint64_t min_ttl) {
    size_t ns = s->num_slots;
    while (ns <= min_ttl) ns <<= 1;
    slot_t *old = s->slots;
    size_t oldn = s->num_slots;
    s->slots = calloc(ns, sizeof(slot_t));
    s->num_slots = ns;
    for (size_t i = 0; i < oldn; i++) {
        for (size_t j = 0; j < old[i].len; j++) {
            uint64_t id = old[i].ids[j];
            place(s, id, s->id_expire[id]);
        }
        free(old[i].ids);
    }
    free(old);
}

static void naive_start(cts_store *s, uint64_t id, uint64_t ttl) {
    ensure_id_cap(s, id);
    if (ttl >= s->num_slots) grow_ring(s, ttl);
    place(s, id, s->now + ttl);
    s->live++;
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

static int naive_stop(cts_store *s, uint64_t id) {
    if (id >= s->id_cap || s->id_slot[id] < 0) return 0;
    slot_remove_at(&s->slots[s->id_slot[id]], s->id_pos[id], s);
    s->id_slot[id] = -1;
    s->live--;
    return 1;
}

static uint64_t naive_tick(cts_store *s) {
    s->now++;
    size_t idx = (size_t)(s->now & (s->num_slots - 1));
    slot_t *sl = &s->slots[idx];
    uint64_t fired = sl->len;
    for (size_t j = 0; j < sl->len; j++) s->id_slot[sl->ids[j]] = -1;
    sl->len = 0;
    s->live -= fired;
    return fired;
}

static uint64_t naive_size(cts_store *s) { return s->live; }

const cts_vtable cts_naive_vtable = {
    "naive", naive_create, naive_destroy,
    naive_start, naive_stop, naive_tick, naive_size,
};
