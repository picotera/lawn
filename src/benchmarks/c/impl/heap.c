/* Binary min-heap over absolute expiry ticks (simplest correct timer
 * baseline: O(log n) insert/delete/tick, no wheel structure at all).
 * An id-indexed position array gives O(log n) delete-by-id. */
#include "cts.h"
#include <stdlib.h>

typedef struct { uint64_t expire; uint64_t id; } hnode_t;

struct cts_store {
    hnode_t  *heap;
    size_t    len, cap;
    int64_t  *pos;      /* id -> heap index, -1 if absent */
    size_t    pos_cap;
    uint64_t  now;
};

static cts_store *heap_create(void) {
    return calloc(1, sizeof(struct cts_store));
}

static void heap_destroy(cts_store *s) {
    free(s->heap); free(s->pos); free(s);
}

static void ensure_pos_cap(struct cts_store *s, uint64_t id) {
    if (id < s->pos_cap) return;
    size_t nc = s->pos_cap ? s->pos_cap : 1024;
    while (id >= nc) nc <<= 1;
    s->pos = realloc(s->pos, nc * sizeof *s->pos);
    for (size_t i = s->pos_cap; i < nc; i++) s->pos[i] = -1;
    s->pos_cap = nc;
}

static void heap_set(struct cts_store *s, size_t i, hnode_t v) {
    s->heap[i] = v;
    s->pos[v.id] = (int64_t)i;
}

static void sift_up(struct cts_store *s, size_t i) {
    while (i > 0) {
        size_t p = (i - 1) / 2;
        if (s->heap[p].expire <= s->heap[i].expire) break;
        hnode_t tmp = s->heap[p];
        heap_set(s, p, s->heap[i]);
        heap_set(s, i, tmp);
        i = p;
    }
}

static void sift_down(struct cts_store *s, size_t i) {
    for (;;) {
        size_t l = 2 * i + 1, r = 2 * i + 2, m = i;
        if (l < s->len && s->heap[l].expire < s->heap[m].expire) m = l;
        if (r < s->len && s->heap[r].expire < s->heap[m].expire) m = r;
        if (m == i) break;
        hnode_t tmp = s->heap[m];
        heap_set(s, m, s->heap[i]);
        heap_set(s, i, tmp);
        i = m;
    }
}

static void heap_start(cts_store *s, uint64_t id, uint64_t ttl) {
    ensure_pos_cap(s, id);
    if (s->len == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 1024;
        s->heap = realloc(s->heap, s->cap * sizeof *s->heap);
    }
    size_t i = s->len++;
    heap_set(s, i, (hnode_t){ s->now + ttl, id });
    sift_up(s, i);
}

static int heap_stop(cts_store *s, uint64_t id) {
    if (id >= s->pos_cap || s->pos[id] < 0) return 0;
    size_t i = (size_t)s->pos[id];
    size_t last_idx = s->len - 1;
    s->pos[id] = -1;
    s->len--;
    if (i != last_idx) {
        heap_set(s, i, s->heap[last_idx]);
        sift_down(s, i);
        sift_up(s, i);
    }
    return 1;
}

static uint64_t heap_tick(cts_store *s) {
    s->now++;
    uint64_t fired = 0;
    while (s->len > 0 && s->heap[0].expire <= s->now) {
        uint64_t id = s->heap[0].id;
        s->pos[id] = -1;
        size_t last_idx = s->len - 1;
        s->len--;
        if (last_idx > 0) {
            heap_set(s, 0, s->heap[last_idx]);
            sift_down(s, 0);
        }
        fired++;
    }
    return fired;
}

static uint64_t heap_size(cts_store *s) { return s->len; }

/* Jump the clock forward with no expiry processing (used for a staggered
 * preload, where target stays below every live deadline so nothing is due). */
static void heap_advance(cts_store *s, uint64_t target) { s->now = target; }

const cts_vtable cts_heap_vtable = {
    "heap", heap_create, heap_destroy,
    heap_start, heap_stop, heap_tick, heap_size, heap_advance,
};
