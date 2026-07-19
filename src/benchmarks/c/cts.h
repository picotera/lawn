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
} cts_vtable;

/* Injected logical clock shared with lawn.c's current_time_ms(). */
extern uint64_t g_cts_now;

/* Registry (defined in bench.c / test.c). */
extern const cts_vtable *const cts_algos[];
extern const int cts_nalgos;

/* Adapters. */
extern const cts_vtable cts_lawn_vtable;
extern const cts_vtable cts_lawn2_vtable;
extern const cts_vtable cts_wahern_vtable;
extern const cts_vtable cts_naive_vtable;

#endif /* CTS_H */
