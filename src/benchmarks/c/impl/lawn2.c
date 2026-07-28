/* cts adapter for lawn2. Nodes live in a slab pool indexed by id so their
 * addresses stay stable (never realloc a live block); no per-insert malloc. */
#include "cts.h"
#include "lawn2.h"
#include <stdlib.h>

struct cts_store {
    lawn2       *l;
    store       *st;
};

static cts_store *l2_create(void) {
    struct cts_store *s = calloc(1, sizeof *s);
    s->l = lawn2_new();
    s->st = init_store();
    return s;
}

static void l2_destroy(cts_store *s) {
    lawn2_free(s->l);
    destroy_store(s->st);
    free(s);
}

static void l2_start(cts_store *s, uint64_t id, uint64_t ttl) {
    lawn2_add(s->l, timer_for(s->st, id), ttl);
}

static int l2_stop(cts_store *s, uint64_t id) {
    lawn2_timer *n = timer_for(s->st, id);
    if (!n->in_store) return 0;
    lawn2_del(s->l, n);
    return 1;
}

static uint64_t l2_tick(cts_store *s) { 
    lawn2_timer *expired_head = NULL;
    uint64_t count = lawn2_tick(s->l, &expired_head);
    return count; 
}
static uint64_t l2_size(cts_store *s) { return lawn2_size(s->l); }

const cts_vtable cts_lawn2_vtable = {
    "lawn2", l2_create, l2_destroy,
    l2_start, l2_stop, l2_tick, l2_size,
};
