/* Correctness gate for the C adapters. Exit non-zero on failure. */
#include "cts.h"
#include "util.h"
#include "impl/lawn2_clamped.h"
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

/* vt->advance must produce identical wheel state to reaching the same point
 * via repeated vt->tick() calls, not just matching totals right after the
 * jump: a cascading structure that strands a timer in the wrong (level,
 * slot) still reports a correct size() and a correct fired-so-far count at
 * that instant, and only diverges once ticking continues (this is exactly
 * how impl/wheel_exact.c's we_advance bug survived the old, lawn2-only,
 * snapshot-only version of this test). For every adapter exposing a
 * non-NULL advance, build two stores with an identical staggered arrival
 * set -- control reaches each arrival via tick(), subject via advance(),
 * exactly mirroring pre_lifecycle's own staggering -- then tick both
 * forward together past every deadline and assert every single per-tick
 * fired count and live size match, not just the final ones. */
static void advance_matches_tick(void) {
    const size_t n = 2000;
    const uint64_t ttl_span = 20000;
    const uint64_t stagger_w = 10000;   /* below every ttl: nothing due during preload */
    const uint64_t run_ticks = 50000;   /* past every deadline (max is 2*stagger_w+ttl_span, i.e. the latest arrival plus the largest shifted ttl), plus slack */

    uint64_t *ttls = malloc(n * sizeof *ttls);
    gen_ttls(ttls, n, ttl_span, 200, WL_UNIFORM, 11);
    /* raw gen_ttls output can be as low as span/distinct, well below
     * stagger_w, so every ttl needs shifting up to guarantee
     * arrival + ttl > stagger_w for every timer (i.e. nothing due
     * during the staggered preload below). */
    for (size_t i = 0; i < n; i++) ttls[i] += stagger_w;

    int checked = 0;
    for (int a = 0; a < cts_nalgos; a++) {
        const cts_vtable *vt = cts_algos[a];
        if (!vt->advance) continue;
        checked++;

        cts_store *ctrl = vt->create();
        cts_store *subj = vt->create();

        uint64_t ctrl_now = 0;
        for (size_t i = 0; i < n; i++) {
            uint64_t arrival = (uint64_t)(((double)i / (double)n) * (double)stagger_w);
            while (ctrl_now < arrival) { vt->tick(ctrl); ctrl_now++; }
            vt->advance(subj, arrival);
            vt->start(ctrl, i, ttls[i]);
            vt->start(subj, i, ttls[i]);
        }
        while (ctrl_now < stagger_w) { vt->tick(ctrl); ctrl_now++; }
        vt->advance(subj, stagger_w);

        for (uint64_t t = 0; t < run_ticks; t++) {
            uint64_t fc = vt->tick(ctrl);
            uint64_t fs = vt->tick(subj);
            if (fc != fs || vt->size(ctrl) != vt->size(subj)) {
                fprintf(stderr,
                        "FAIL advance_matches_tick: %s tick %llu: ctrl_fired=%llu subj_fired=%llu "
                        "ctrl_size=%llu subj_size=%llu\n",
                        vt->name, (unsigned long long)t,
                        (unsigned long long)fc, (unsigned long long)fs,
                        (unsigned long long)vt->size(ctrl), (unsigned long long)vt->size(subj));
                exit(1);
            }
        }
        vt->destroy(ctrl);
        vt->destroy(subj);
    }
    free(ttls);
    printf("  advance_matches_tick: %d impls' advance() reproduces the full tick-driven "
           "expiry sequence over %llu ticks after a staggered preload\n",
           checked, (unsigned long long)run_ticks);
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
