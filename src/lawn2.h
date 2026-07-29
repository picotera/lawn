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
#include <stdlib.h>

/* Intrusive node: embed in your object (wahern struct-timeout style). The
 * caller owns storage; lawn2 only links it into the per-TTL queue. */
typedef struct lawn2_timer {
    uint64_t ttl;                 /* bucket key, set by lawn2_add          */
    uint64_t expiration;          /* absolute expiry (now_at_add + ttl)    */
    struct lawn2_timer *next, *prev;
    int in_store;                 /* 1 while linked; guards double del/fire */
    uint64_t id;
} lawn2_timer;

typedef struct lawn2 lawn2;


// ############### Timeouts Storage (optional) ####################
/*  This is an optional implementation of a minimal block based caller node (each representing  
 *  a single timer) storage for use with Lawn. Users are welcome to replace this implementation 
 *  with any other (more suitable for their needs) data structure for storing the nodes 
 *  provided to the Lawn Timer Storage.
*/

typedef struct caller_state_store {
    lawn2_timer **blocks;   /* array of stable node blocks */
    size_t       nblocks;
} timer_store;

timer_store *init_store(void); /* Init a caller nodes store for a set timer nodes*/
lawn2_timer *timer_for(timer_store *s, uint64_t id); /* Init and store a caller node with a given ID in the provided store */
void destroy_store(timer_store *s); /* frees the caller nodes store */

// ############## Timer Storage ####################

lawn2   *lawn2_new(void);
void     lawn2_free(lawn2 *l);          /* frees the store, not caller nodes */

void     lawn2_add(lawn2 *l, lawn2_timer *n, uint64_t ttl); // Push, O(1)
void     lawn2_del(lawn2 *l, lawn2_timer *n); // Pull, O(1)
uint64_t lawn2_tick(lawn2 *l, lawn2_timer **out_head); // Poll: +1 tick, return #expired and populate list of exired nodes in out_head
uint64_t lawn2_size(lawn2 *l);
uint64_t lawn2_now(lawn2 *l);

uint64_t lawn2_next_expiration(lawn2 *l);
void     lawn2_set_now(lawn2 *l, uint64_t now);

#endif /* LAWN2_H */
