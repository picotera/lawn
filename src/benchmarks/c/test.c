/* Correctness gate for the C adapters. Exit non-zero on failure. */
#include "cts.h"
#include "util.h"
#include "impl/lawn2_clamped.h"
#include "lawn2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define N 5000
#define TICKS 1000

/* lawn2clamp deliberately reschedules ttls onto coarser wheel buckets, so it
 * is excluded from the exact-schedule checks below; clamp_math/clamp_wiring
 * cover its own correctness. */
static int is_lossy(const cts_vtable *vt) {
    return !strcmp(vt->name, "lawn2clamp");
}

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
    int checked = 1;
    for (int a = 1; a < cts_nalgos; a++) {
        if (is_lossy(cts_algos[a])) continue;
        checked++;
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
           del_stride, checked, TICKS);
}

/* A large TTL spanning multiple wheel levels / a ring grow must fire on its
 * exact tick, and only then. */
static void overflow_exact(void) {
    uint64_t ttls[] = {255, 256, 257, 16383, 16384, 20000, 300000};
    for (size_t k = 0; k < sizeof ttls / sizeof ttls[0]; k++) {
        uint64_t ttl = ttls[k];
        for (int a = 0; a < cts_nalgos; a++) {
            const cts_vtable *vt = cts_algos[a];
            if (is_lossy(vt)) continue;
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

/* lawn2_advance must fire exactly what repeated lawn2_tick calls would have,
 * just in one jump instead of one call per elapsed tick. */
static void advance_matches_tick(void) {
    const size_t n = 3000;
    const uint64_t target = 60000;
    uint64_t *ttls = malloc(n * sizeof *ttls);
    gen_ttls(ttls, n, 50000, 500, WL_UNIFORM, 7);

    timer_store *st_a = init_store(), *st_b = init_store();
    lawn2 *a = lawn2_new(), *b = lawn2_new();
    for (size_t i = 0; i < n; i++) {
        lawn2_add(a, timer_for(st_a, i), ttls[i]);
        lawn2_add(b, timer_for(st_b, i), ttls[i]);
    }
    free(ttls);

    uint64_t fired_tick = 0;
    for (uint64_t t = 0; t < target; t++) fired_tick += lawn2_tick(a, NULL);
    uint64_t fired_adv = lawn2_advance(b, target, NULL);

    if (fired_tick != fired_adv || lawn2_size(a) != lawn2_size(b) ||
        lawn2_now(a) != lawn2_now(b)) {
        fprintf(stderr,
                "FAIL advance_matches_tick: tick_fired=%llu adv_fired=%llu "
                "size_a=%llu size_b=%llu now_a=%llu now_b=%llu\n",
                (unsigned long long)fired_tick, (unsigned long long)fired_adv,
                (unsigned long long)lawn2_size(a), (unsigned long long)lawn2_size(b),
                (unsigned long long)lawn2_now(a), (unsigned long long)lawn2_now(b));
        exit(1);
    }
    lawn2_free(a); lawn2_free(b);
    destroy_store(st_a); destroy_store(st_b);
    printf("  lawn2_advance: matches %llu ticks of lawn2_tick over %zu timers\n",
           (unsigned long long)target, n);
}

/* Pure-function invariants for the Linux-kernel-style TTL clamp used by
 * lawn2clamp (impl/lawn2_clamped.c): never fires early, and an already
 * bucket-aligned ttl is a fixed point. Covers every level, including the
 * level-8 catch-all, without ticking a store through tens of millions of
 * ticks. */
static void clamp_math(void) {
    static const uint64_t ttls[] = {
        0, 1, 63, 64, 65, 511, 512, 4095, 4096, 32767, 32768,
        262143, 262144, 2097151, 2097152, 16777215, 16777216,
        134217727, 134217728, 200000000,
    };
    for (size_t k = 0; k < sizeof ttls / sizeof ttls[0]; k++) {
        uint64_t ttl = ttls[k];
        uint64_t clamped = clamp_timer_ttl(ttl);
        uint64_t reclamped = clamp_timer_ttl(clamped);
        if (clamped < ttl || reclamped != clamped) {
            fprintf(stderr, "FAIL clamp_math: ttl=%llu clamp=%llu clamp(clamp)=%llu\n",
                    (unsigned long long)ttl, (unsigned long long)clamped,
                    (unsigned long long)reclamped);
            exit(1);
        }
    }
    printf("  clamp_timer_ttl: %zu ttls never fire early and are bucket-stable\n",
           sizeof ttls / sizeof ttls[0]);
}

/* End-to-end: lawn2clamp must actually hand lawn2 the clamped ttl, not the
 * raw one. Kept to small ttls so the fire-tick search stays cheap. */
static void clamp_wiring(void) {
    static const uint64_t ttls[] = { 1, 63, 64, 65, 100, 511, 512, 1000, 4095, 4096 };
    const cts_vtable *vt = &cts_lawn2_clamped_vtable;
    for (size_t k = 0; k < sizeof ttls / sizeof ttls[0]; k++) {
        uint64_t ttl = ttls[k];
        uint64_t expected = clamp_timer_ttl(ttl);
        cts_store *s = vt->create();
        vt->start(s, 7, ttl);
        uint64_t fired_at = 0;
        for (uint64_t t = 1; t <= expected; t++) {
            if (vt->tick(s)) { fired_at = t; break; }
        }
        if (fired_at != expected || vt->size(s) != 0) {
            fprintf(stderr, "FAIL clamp_wiring: ttl=%llu expected=%llu fired_at=%llu size=%llu\n",
                    (unsigned long long)ttl, (unsigned long long)expected,
                    (unsigned long long)fired_at, (unsigned long long)vt->size(s));
            exit(1);
        }
        vt->destroy(s);
    }
    printf("  lawn2clamp: %zu ttls fire on their clamped tick via lawn2\n",
           sizeof ttls / sizeof ttls[0]);
}

int main(void) {
    printf("C correctness gate (%d impls: ", cts_nalgos);
    for (int a = 0; a < cts_nalgos; a++) printf("%s%s", cts_algos[a]->name,
                                                a + 1 < cts_nalgos ? ", " : ")\n");
    differential(0);
    differential(7);
    overflow_exact();
    advance_matches_tick();
    clamp_math();
    clamp_wiring();
    printf("ALL C CORRECTNESS TESTS PASSED\n");
    return 0;
}
