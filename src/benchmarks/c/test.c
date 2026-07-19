/* Correctness gate for the C adapters. Exit non-zero on failure. */
#include "cts.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define N 5000
#define TICKS 1000

/* Run one impl on a fixed (id,ttl) set, recording per-tick expiry counts.
 * If del_stride>0, delete every del_stride-th id before ticking. */
static void schedule(const cts_vtable *vt, const uint64_t *ttls,
                     uint64_t *seq, int del_stride) {
    cts_store *s = vt->create();
    for (uint64_t i = 0; i < N; i++) vt->start(s, i, ttls[i]);
    uint64_t expected_live = N;
    if (del_stride > 0) {
        for (uint64_t i = 0; i < N; i += del_stride) {
            int r = vt->stop(s, i);
            if (r) expected_live--;
        }
        if (vt->size(s) != expected_live) {
            fprintf(stderr, "FAIL %s: size %llu after delete, expected %llu\n",
                    vt->name, (unsigned long long)vt->size(s),
                    (unsigned long long)expected_live);
            exit(1);
        }
    }
    for (int t = 0; t < TICKS; t++) seq[t] = vt->tick(s);
    vt->destroy(s);
}

static void differential(int del_stride) {
    uint64_t *ttls = malloc(N * sizeof *ttls);
    gen_ttls(ttls, N, 1500, 300, WL_UNIFORM, 42);
    uint64_t *ref = malloc(TICKS * sizeof *ref);
    uint64_t *cur = malloc(TICKS * sizeof *cur);
    schedule(cts_algos[0], ttls, ref, del_stride);
    for (int a = 1; a < cts_nalgos; a++) {
        schedule(cts_algos[a], ttls, cur, del_stride);
        for (int t = 0; t < TICKS; t++) {
            if (cur[t] != ref[t]) {
                fprintf(stderr, "FAIL differential (del=%d): tick %d, %s=%llu %s=%llu\n",
                        del_stride, t, cts_algos[0]->name,
                        (unsigned long long)ref[t], cts_algos[a]->name,
                        (unsigned long long)cur[t]);
                exit(1);
            }
        }
    }
    free(ttls); free(ref); free(cur);
    printf("  differential (del_stride=%d): %d impls agree over %d ticks\n",
           del_stride, cts_nalgos, TICKS);
}

/* A large TTL spanning multiple wheel levels / a ring grow must fire on its
 * exact tick, and only then. */
static void overflow_exact(void) {
    uint64_t ttls[] = {255, 256, 257, 16383, 16384, 20000, 300000};
    for (size_t k = 0; k < sizeof ttls / sizeof ttls[0]; k++) {
        uint64_t ttl = ttls[k];
        for (int a = 0; a < cts_nalgos; a++) {
            const cts_vtable *vt = cts_algos[a];
            cts_store *s = vt->create();
            vt->start(s, 42, ttl);
            uint64_t fired_at = 0;
            for (uint64_t t = 1; t <= ttl; t++) {
                if (vt->tick(s)) { fired_at = t; break; }
            }
            if (fired_at != ttl || vt->size(s) != 0) {
                fprintf(stderr, "FAIL overflow %s: ttl=%llu fired_at=%llu size=%llu\n",
                        vt->name, (unsigned long long)ttl,
                        (unsigned long long)fired_at, (unsigned long long)vt->size(s));
                exit(1);
            }
            vt->destroy(s);
        }
    }
    printf("  overflow/exact-tick: all impls fire large TTLs on the exact tick\n");
}

int main(void) {
    printf("C correctness gate (%d impls: ", cts_nalgos);
    for (int a = 0; a < cts_nalgos; a++) printf("%s%s", cts_algos[a]->name,
                                                a + 1 < cts_nalgos ? ", " : ")\n");
    differential(0);
    differential(7);
    overflow_exact();
    printf("ALL C CORRECTNESS TESTS PASSED\n");
    return 0;
}
