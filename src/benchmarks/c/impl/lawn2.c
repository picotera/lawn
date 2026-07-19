/* cts adapter for lawn2. Nodes live in a slab pool indexed by id so their
 * addresses stay stable (never realloc a live block); no per-insert malloc. */
#include "cts.h"
#include "lawn2.h"
#include <stdlib.h>

#define BLK_BITS 12
#define BLK_SIZE (1u << BLK_BITS)
#define BLK_MASK (BLK_SIZE - 1)

struct cts_store {
    lawn2       *l;
    lawn2_node **blocks;   /* array of stable node blocks */
    size_t       nblocks;
};

static lawn2_node *node_for(struct cts_store *s, uint64_t id) {
    size_t bi = (size_t)(id >> BLK_BITS);
    if (bi >= s->nblocks) {
        size_t old = s->nblocks;
        s->nblocks = bi + 1;
        s->blocks = realloc(s->blocks, s->nblocks * sizeof *s->blocks);
        for (size_t i = old; i < s->nblocks; i++)
            s->blocks[i] = calloc(BLK_SIZE, sizeof(lawn2_node));
    }
    return &s->blocks[bi][id & BLK_MASK];
}

static cts_store *l2_create(void) {
    struct cts_store *s = calloc(1, sizeof *s);
    s->l = lawn2_new();
    return s;
}

static void l2_destroy(cts_store *s) {
    lawn2_free(s->l);
    for (size_t i = 0; i < s->nblocks; i++) free(s->blocks[i]);
    free(s->blocks);
    free(s);
}

static void l2_start(cts_store *s, uint64_t id, uint64_t ttl) {
    lawn2_add(s->l, node_for(s, id), ttl);
}

static int l2_stop(cts_store *s, uint64_t id) {
    lawn2_node *n = node_for(s, id);
    if (!n->in_store) return 0;
    lawn2_del(s->l, n);
    return 1;
}

static uint64_t l2_tick(cts_store *s) { return lawn2_tick(s->l); }
static uint64_t l2_size(cts_store *s) { return lawn2_size(s->l); }

const cts_vtable cts_lawn2_vtable = {
    "lawn2", l2_create, l2_destroy,
    l2_start, l2_stop, l2_tick, l2_size,
};
