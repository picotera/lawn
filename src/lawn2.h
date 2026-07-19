/* lawn2 - Queue-Map (Lawn) timer store, allocation-lean rewrite.
 *
 * Same algorithm as src/lawn.c (docs/Algorithm.md "Queue-Map Algorithm"): a map
 * of TTL -> self-sorted queue; Push O(1), Pull O(1), Poll O(max(x,t)). It only
 * changes the implementation to drop the per-insert tax of the original:
 *   - intrusive, caller-owned nodes  -> no per-insert node malloc, no key copy
 *   - O(1) handle delete via node links -> no element-id hashmap, no key hash
 *   - open-addressing TTL->queue table -> no per-entry malloc (vs libbpf chain)
 *   - next_expiration lower bound      -> O(1) empty ticks (as in src/lawn.c)
 *
 * Self-contained C11 (no glibc-only headers), portable like the wahern wheel.
 */
#ifndef LAWN2_H
#define LAWN2_H

#include <stdint.h>

/* Intrusive node: embed in your object (wahern struct-timeout style). The
 * caller owns storage; lawn2 only links it into the per-TTL queue. */
typedef struct lawn2_node {
    uint64_t ttl;                 /* bucket key, set by lawn2_add          */
    uint64_t expiration;          /* absolute expiry (now_at_add + ttl)    */
    struct lawn2_node *next, *prev;
    int in_store;                 /* 1 while linked; guards double del/fire */
} lawn2_node;

typedef struct lawn2 lawn2;

lawn2   *lawn2_new(void);
void     lawn2_free(lawn2 *l);          /* frees the store, not caller nodes */

void     lawn2_add(lawn2 *l, lawn2_node *n, uint64_t ttl); /* Push, O(1)      */
void     lawn2_del(lawn2 *l, lawn2_node *n);               /* Pull, O(1)      */
uint64_t lawn2_tick(lawn2 *l);          /* Poll: +1 tick, return #expired    */
uint64_t lawn2_size(lawn2 *l);
uint64_t lawn2_now(lawn2 *l);

#endif /* LAWN2_H */
