/* C timer-store: uniform vtable over timer data structures, logical clock.
 * Mirrors the Python TimerStore protocol. TTLs and time are integer ticks. */
#ifndef CTS_H
#define CTS_H

#include <stdint.h>
#include <stddef.h>

typedef struct cts_store cts_store;

typedef struct {
    const char *name;
    cts_store *(*create)(void);
    void       (*destroy)(cts_store *);
    void       (*start)(cts_store *, uint64_t id, uint64_t ttl_ticks);
    int        (*stop)(cts_store *, uint64_t id);   /* 1 removed, 0 absent */
    uint64_t   (*tick)(cts_store *);                /* advance 1 tick, #expired */
    uint64_t   (*size)(cts_store *);
    /* Optional: fast-forward the logical clock to `target` (O(1)/O(levels),
     * not tick-by-tick) so a preload can be inserted at staggered arrival
     * times. NULL if the store can't jump its clock cheaply. */
    void       (*advance)(cts_store *, uint64_t target);
} cts_vtable;

/* Injected logical clock shared with lawn.c's current_time_ms(). */
extern uint64_t g_cts_now;

/* Registry (defined in bench.c / test.c). */
extern const cts_vtable *const cts_algos[];
extern const int cts_nalgos;

/* Adapters. */
extern const cts_vtable cts_lawn_vtable;
extern const cts_vtable cts_lawn2_vtable;
extern const cts_vtable cts_lawn2_clamped_vtable;
extern const cts_vtable cts_wahern_vtable;
extern const cts_vtable cts_naive_vtable;
extern const cts_vtable cts_heap_vtable;
extern const cts_vtable cts_wheel_exact_vtable;

#endif /* CTS_H */
