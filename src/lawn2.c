/* lawn2 implementation - see lawn2.h. */
#include "lawn2.h"

#define GOLDEN 0x9E3779B97F4A7C15ULL

/* One TTL blade: head is the earliest expiry (queue is self-sorted because
 * same-TTL timers arrive in expiry order). Empty-but-used slots linger until a
 * grow rehashes them out; the same TTLs reappear in practice, so t stays small. */
typedef struct {
    uint64_t   ttl;
    int        used;
    lawn2_timer *head, *tail;
} blade;

struct lawn2 {
    uint64_t now;
    blade  *tab;
    size_t   cap;            /* power of two */
    unsigned bits;           /* cap == 1u << bits */
    size_t   count;          /* used slots (distinct TTLs seen) */
    uint64_t live;
    uint64_t next_expiration;/* lower bound on earliest live expiry */
};


// ##################### timer storage #################################


#define BLK_BITS 12
#define BLK_SIZE (1u << BLK_BITS)
#define BLK_MASK (BLK_SIZE - 1)

lawn2_timer *timer_for(timer_store *s, uint64_t id) {
    size_t block_index = (size_t)(id >> BLK_BITS);
    if (block_index >= s->nblocks) {
        size_t old = s->nblocks;
        s->nblocks = block_index + 1;
        s->blocks = realloc(s->blocks, s->nblocks * sizeof *s->blocks);
        for (size_t i = old; i < s->nblocks; i++) {
            s->blocks[i] = calloc(BLK_SIZE, sizeof(lawn2_timer));
            uint64_t base_id = (uint64_t)i << BLK_BITS; // Calc base ID offset for block i
            for (size_t j = 0; j < BLK_SIZE; j++) { // Fill in ids for all nodes in block
                s->blocks[i][j].id = base_id | j; 
            }
        }
    }
    return &s->blocks[block_index][id & BLK_MASK];
}

timer_store *init_store(void) {
    timer_store *s = calloc(1, sizeof *s);
    return s;
}

void destroy_store(timer_store *s) {
    for (size_t i = 0; i < s->nblocks; i++) free(s->blocks[i]);
    free(s->blocks);
    free(s);
}

// ###################### internal datastructure managment #############

static blade *find_slot(lawn2 *l, uint64_t ttl) {
    size_t mask = l->cap - 1;
    size_t i = (size_t)((ttl * GOLDEN) >> (64 - l->bits));
    for (;;) {
        blade *b = &l->tab[i & mask];
        if (!b->used || b->ttl == ttl) return b;
        i++;
    }
}

static void grow(lawn2 *l) {
    unsigned nb = l->bits + 1;
    size_t ncap = (size_t)1 << nb;
    blade *ot = l->tab;
    size_t ocap = l->cap;
    l->tab = calloc(ncap, sizeof(blade));
    l->cap = ncap;
    l->bits = nb;
    size_t new_count = 0;
    for (size_t i = 0; i < ocap; i++) {
        if (ot[i].used) {
            blade *b = find_slot(l, ot[i].ttl);  /* empty in the new table */
            *b = ot[i];                           /* carries head/tail ptrs */
            new_count++;
        }
    }
    l->count = new_count;
    free(ot);
}

// ######################## user facing APIs ###########################


lawn2 *lawn2_new(void) {
    lawn2 *l = calloc(1, sizeof *l);
    l->bits = 4;
    l->cap = (size_t)1 << l->bits;
    l->tab = calloc(l->cap, sizeof(blade));
    l->next_expiration = UINT64_MAX;
    return l;
}

void lawn2_free(lawn2 *l) {
    if (!l) return;
    free(l->tab);
    free(l);
}

void lawn2_add(lawn2 *l, lawn2_timer *n, uint64_t ttl) {
    if ((l->count + 1) * 10 >= l->cap * 7) grow(l);  /* keep load < 0.7 */
    blade *b = find_slot(l, ttl);
    if (!b->used) {
        b->used = 1;
        b->ttl = ttl;
        b->head = b->tail = NULL;
        l->count++;
    }
    n->ttl = ttl;
    n->expiration = l->now + ttl;
    n->in_store = 1;
    /* append at tail; head stays the earliest */
    n->next = NULL;
    n->prev = b->tail;
    if (b->tail) b->tail->next = n; else b->head = n;
    b->tail = n;
    l->live++;
    if (n->expiration < l->next_expiration) l->next_expiration = n->expiration;
}

void lawn2_del(lawn2 *l, lawn2_timer *n) {
    if (!n->in_store) return;
    blade *b = find_slot(l, n->ttl);            /* exists */
    if (n->prev) n->prev->next = n->next; else b->head = n->next;
    if (n->next) n->next->prev = n->prev; else b->tail = n->prev;
    n->next = n->prev = NULL;
    n->in_store = 0;
    l->live--;
    /* next_expiration stays a valid lower bound (removal only delays expiry). and this will update on the next tick either way */
}

uint64_t lawn2_tick(lawn2 *l, lawn2_timer **out_head) {
    if (out_head) *out_head = NULL;
    l->now++;

    if (l->now < l->next_expiration) return 0;   /* O(1) empty tick */

    uint64_t now = l->now, fired = 0;
    uint64_t ne = UINT64_MAX; /* recompute over t heads in flight */
    for (size_t i = 0; i < l->cap; i++) {
        blade *b = &l->tab[i];
        if (!b->used) continue;

        while (b->head && b->head->expiration <= now) {  /* self-sorted head */
            lawn2_timer *n = b->head;
            b->head = n->next; /* Unlink from blade */
            if (b->head) b->head->prev = NULL; else b->tail = NULL;
            n->next = n->prev = NULL;
            n->in_store = 0;

            /* Push onto output singly-linked list */
            if (out_head) {
                n->next = *out_head;
                n->prev = NULL;
                *out_head = n;
            } else {
                n->next = n->prev = NULL;
            }

            fired++;
        }
        if (b->head && b->head->expiration < ne)
            ne = b->head->expiration;
    }
    l->live -= fired;
    l->next_expiration = ne;
    return fired;
}

uint64_t lawn2_size(lawn2 *l) { 
    return l->live;
}

uint64_t lawn2_now(lawn2 *l) { 
    return l->now;
}

uint64_t lawn2_next_expiration(lawn2 *l) {
    return l ? l->next_expiration : UINT64_MAX;
}

void lawn2_set_now(lawn2 *l, uint64_t now) {
    if (l) l->now = now;
}
