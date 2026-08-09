/* C benchmark harness: mirrors src/benchmarks/python, emits CSVs matching the
 * Python schema so the same plotting renders them. Generic COW-based main
 * loop: the parent pre-generates a unified scenario (TTLs, rng rolls) once
 * per sweep point, and each forked child inherits that scenario read-only via
 * copy-on-write, aggregating metrics internally for minimal IPC overhead. */
#include "cts.h"
#include "util.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <malloc/malloc.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_host.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#endif

#define SEED   1234
#define MEM_SAFETY_PCT 90.0
#define MAX_OPS 20000
#define OP_PER_N -1
#define TICKS   2000
#define WINDOW  200
#define BATCH   256
#define TICK_SCAN_SAMPLES (4 * BATCH)

#define BASE_N (100 * 1000)
#define BASE_SPAN 1024
#define BASE_N_HUGE (10 * 1000 * 1000)
#define BASE_SPAN_HUGE 65536
/* Fixed count of timed foreground ops for the lifecycle measurements whose
 * population is set separately via p.preload_n (the extended inflection sweep
 * and the lifecycle n-axis sweep): keeps the op count constant so only the
 * background population and distinct-TTL count vary. */
#define FG_OPS 5000

typedef struct {
    size_t   n;
    uint64_t ttl_span;
    uint64_t distinct;
    int      workload;
    /* Only the lifecycle op reads this. 0 (the default for every existing
     * literal params_t{} initializer, unspecified trailing members are
     * zero-initialized) means "use the regime-based default"; nonzero
     * overrides preload_timer_count directly, decoupling population size
     * from the fixed BASE_N/BASE_N_HUGE regime switch. */
    uint64_t preload_n;
} params_t;

static const char *PARAMETER_NAMES[] = {"n", "ttl_span", "distinct_ttls", "workload"};
#define BASE_PARAMS {BASE_N, BASE_SPAN, 100, WL_UNIFORM, 0}
#define GET_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static const size_t N_VALS[] = {1, 10, 100, 1000};
static const size_t N_VALS_HUGE[] = {2, 5, 10, 20, 50, 80, 100, 110, 120, 150, 200};
/* Chosen by a dedicated sweep (lawn2 vs wahern, n=100K, powers of two from
 * 128 to 65536) rather than picked arbitrarily: the wheel's tick cost is
 * genuinely non-monotonic in span (a reproducible dip near 2048, a spike
 * near 4096, tied to its internal hierarchical level boundaries, not noise:
 * confirmed by rerunning the sweep independently). 1024 is the smallest
 * value past the steep small-span regime and sits on neither the dip nor
 * the spike. The span-sweep axis itself uses the same grid. */
static const uint64_t SPAN_VALS[] = {128, 1024, 4096, 8192, 65536, 524288};
static const uint64_t DISTINCT_TTL_VALS[] = {1, 10, 100, 1000, 10000};
static const int WORKLOAD_VALS[] = {WL_UNIFORM, WL_BURSTY, WL_SPREAD};
static const char *WORKLOAD_NAMES[] = {"uniform", "bursty", "spread"};


/* ---- Generic Scenario & Callback Types ---- */
typedef struct {
    params_t p;
    uint64_t *ttls;         
    
    uint64_t *delete_perm;
    
    uint64_t *tick_sorted;
    size_t tick_nsamp;
    
    uint64_t lc_preload_timer_count;
    uint64_t *lc_preload_ttls;
    uint64_t lc_preload_distinct;
    uint64_t *lc_preload_bucket_vals;
    uint64_t lc_fg_id_base;
    double lc_preload_replenish_rate;
    int lc_stagger;
    uint64_t lc_stagger_W;
    double *lc_rolls_op;            
    uint64_t *lc_rolls_del_idx;     
    uint64_t *lc_rolls_replenish;   
} scenario_t;

typedef void (*setup_fn)(scenario_t *sc);
typedef void (*teardown_fn)(scenario_t *sc);
typedef void (*pre_fn)(const cts_vtable *vt, cts_store *s, const scenario_t *sc, void **run_state);
typedef size_t (*payload_fn)(const cts_vtable *vt, cts_store *s, const scenario_t *sc, void *run_state, double *out);
typedef void (*post_fn)(const cts_vtable *vt, cts_store *s, const scenario_t *sc, void *run_state);

typedef struct {
    const char *name;
    size_t per_run_max;
    setup_fn setup;
    pre_fn pre;
    payload_fn payload;
    post_fn post;
    teardown_fn teardown;
} op_t;

/* Shared pre_fn for operations that just start all timers */
static void pre_start_all(const cts_vtable *vt, cts_store *s, const scenario_t *sc, void **run_state) {
    (void)run_state;
    for (size_t i = 0; i < sc->p.n; i++) vt->start(s, i, sc->ttls[i]);
}

/* ---- 1. Insert Operation ---- */
static void pre_insert(const cts_vtable *vt, cts_store *s, const scenario_t *sc, void **run_state) {
    (void)run_state;
    size_t timed = sc->p.n < MAX_OPS ? sc->p.n : MAX_OPS;
    size_t pre = sc->p.n - timed;
    for (size_t i = 0; i < pre; i++) vt->start(s, i, sc->ttls[i]);
}

static size_t payload_insert(const cts_vtable *vt, cts_store *s, const scenario_t *sc, void *run_state, double *out) {
    (void)run_state;
    size_t timed = sc->p.n < MAX_OPS ? sc->p.n : MAX_OPS;
    size_t pre = sc->p.n - timed, c = 0;
    for (size_t i = pre; i < sc->p.n; ) {
        size_t bs = (sc->p.n - i) < BATCH ? (sc->p.n - i) : BATCH;
        uint64_t t0 = cts_now_ns();
        for (size_t k = 0; k < bs; k++) vt->start(s, i + k, sc->ttls[i + k]);
        out[c++] = (double)(cts_now_ns() - t0) / (double)bs;
        i += bs;
    }
    return c;
}

/* ---- 2. Delete Operation ---- */
static void setup_delete(scenario_t *sc) {
    sc->delete_perm = malloc(sc->p.n * sizeof(uint64_t));
    gen_shuffle(sc->delete_perm, sc->p.n, SEED);
}

static size_t payload_delete(const cts_vtable *vt, cts_store *s, const scenario_t *sc, void *run_state, double *out) {
    (void)run_state;
    size_t timed = sc->p.n < MAX_OPS ? sc->p.n : MAX_OPS, c = 0;
    for (size_t k = 0; k < timed; ) {
        size_t bs = (timed - k) < BATCH ? (timed - k) : BATCH;
        uint64_t t0 = cts_now_ns();
        for (size_t j = 0; j < bs; j++) vt->stop(s, sc->delete_perm[k + j]);
        out[c++] = (double)(cts_now_ns() - t0) / (double)bs;
        k += bs;
    }
    for (size_t k = timed; k < sc->p.n; k++) vt->stop(s, sc->delete_perm[k]);
    return c;
}

static void teardown_delete(scenario_t *sc) { free(sc->delete_perm); }

/* ---- 3. Empty Tick Operation ---- */
static void setup_empty_tick(scenario_t *sc) {
    for (size_t i = 0; i < sc->p.n; i++) sc->ttls[i] += TICKS;
}

/* Pure idle-tick (guard-hit) cost: every timer's TTL is shifted past the
 * whole measured window, so nothing is ever due and every tick() call must
 * take the O(1) empty-tick fast path, for every algorithm, not just the
 * ones with an explicit "closest expiration" guard. Verified, not assumed:
 * post_empty_tick asserts vt->size() never changed across the whole window,
 * so a bug that fired something early would fail loudly here instead of
 * silently contaminating the measurement. */
static void pre_empty_tick(const cts_vtable *vt, cts_store *s, const scenario_t *sc, void **run_state) {
    pre_start_all(vt, s, sc, run_state);
    *run_state = (void*)(uintptr_t)vt->size(s); 
}

static size_t payload_empty_tick(const cts_vtable *vt, cts_store *s, const scenario_t *sc, void *run_state, double *out) {
    (void)sc; (void)run_state;
    size_t c = 0;
    for (int t = 0; t < TICKS; ) {
        int bs = (TICKS - t) < BATCH ? (TICKS - t) : BATCH;
        uint64_t t0 = cts_now_ns();
        for (int j = 0; j < bs; j++) vt->tick(s);
        out[c++] = (double)(cts_now_ns() - t0) / (double)bs;
        t += bs;
    }
    return c;
}

static void post_empty_tick(const cts_vtable *vt, cts_store *s, const scenario_t *sc, void *run_state) {
    (void)sc;
    if (vt->size(s) != (uint64_t)(uintptr_t)run_state) abort();
}

/* ---- 4. Expiry Operation ---- */
static void setup_expiry(scenario_t *sc) {
    gen_ttls(sc->ttls, sc->p.n, WINDOW, sc->p.distinct, sc->p.workload, SEED);
}

static size_t payload_expiry(const cts_vtable *vt, cts_store *s, const scenario_t *sc, void *run_state, double *out) {
    (void)sc; (void)run_state;
    size_t c = 0;
    for (int t = 0; t < WINDOW; ) {
        int bs = (WINDOW - t) < BATCH ? (WINDOW - t) : BATCH;
        uint64_t t0 = cts_now_ns();
        for (int j = 0; j < bs; j++) vt->tick(s);
        out[c++] = (double)(cts_now_ns() - t0) / (double)bs;
        t += bs;
    }
    return c;
}

/* ---- 5. Memory Operation ---- */
static size_t payload_memory(const cts_vtable *vt, cts_store *s_unused, const scenario_t *sc, void *run_state, double *out) {
    (void)s_unused; (void)run_state;
    malloc_statistics_t before, after;
    malloc_zone_statistics(malloc_default_zone(), &before);
    
    cts_store *s = vt->create();
    for (size_t i = 0; i < sc->p.n; i++) vt->start(s, i, sc->ttls[i]);
    
    malloc_zone_statistics(malloc_default_zone(), &after);
    vt->destroy(s);
    
    double bytes = (double)after.size_in_use - (double)before.size_in_use;
    out[0] = bytes < 0 ? 0 : bytes;
    return 1;
}

/* ---- 6. Tick Scan Operation ----
 * Reports the cost of a tick that actually does a scan (crosses a live
 * bucket's expiry), not an idle guard-hit tick (tick_advance already
 * covers that). gen_ttls is a deterministic function of
 * (n, ttl_span, distinct, workload, SEED), so the exact set of populated
 * bucket values is just the sorted, deduplicated ttls[] array already
 * generated for the scenario, no need to guess or re-derive gen_ttls'
 * bucket formula. For each populated value, fast-forward (untimed) to one
 * tick before it, cheap regardless of how far out it sits since every tick
 * along the way is a genuine empty tick, then time only the single tick
 * that crosses the boundary. Samples up to TICK_SCAN_SAMPLES such boundary
 * ticks (fewer if the population has fewer distinct live values than
 * that), so runtime stays bounded regardless of ttl_span or how sparse the
 * boundaries are, unlike timing a full drain. */
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static void setup_tick_scan(scenario_t *sc) {
    sc->tick_sorted = malloc(sc->p.n * sizeof(uint64_t));
    memcpy(sc->tick_sorted, sc->ttls, sc->p.n * sizeof(uint64_t));
    qsort(sc->tick_sorted, sc->p.n, sizeof(uint64_t), cmp_u64);
    
    size_t nvals = 0;
    for (size_t i = 0; i < sc->p.n; i++)
        if (i == 0 || sc->tick_sorted[i] != sc->tick_sorted[i - 1]) sc->tick_sorted[nvals++] = sc->tick_sorted[i];
        
    sc->tick_nsamp = nvals < TICK_SCAN_SAMPLES ? nvals : TICK_SCAN_SAMPLES;
}

static size_t payload_tick_scan(const cts_vtable *vt, cts_store *s, const scenario_t *sc, void *run_state, double *out) {
    (void)run_state;
    uint64_t current = 0;
    size_t c = 0;
    for (size_t i = 0; i < sc->tick_nsamp; i++) {
        uint64_t target = sc->tick_sorted[i];
        while (current < target - 1) { vt->tick(s); current++; }
        uint64_t t0 = cts_now_ns();
        vt->tick(s); current++;
        out[c++] = (double)(cts_now_ns() - t0);
    }
    return c;
}

static void teardown_tick_scan(scenario_t *sc) { free(sc->tick_sorted); }

/* ---- 7. Lifecycle Operation ----
 * Realistic-mix stress test: pre-fills a realistic, fixed-size preload
 * population, then runs p.n iterations of randomly interleaved insert,
 * delete, and tick, timing each individual operation and returning every
 * one of those per-op latencies (not a single summary number), aggregated
 * the same way as every other op here: full mean/std/p99/max over the
 * pooled samples. Isolated per-op sweeps elsewhere in this file can't
 * surface a stall that only happens when one op type's worst case (e.g. a
 * resize, a cascade) lands while other operations are in flight; this
 * does, by construction.
 *
 * Preload size and span are deliberately NOT derived from p.n: a real
 * system's ambient population and TTL range don't grow just because a
 * stress test asks for more samples, and tying them to p.n produces a
 * choice of two failure modes tested and rejected here: a decaying
 * background (if span is a fixed fraction of p.n, most of the preload
 * expires by the run's midpoint, biasing "n" toward an average over a
 * shrinking population that's mostly below n) or an O(n^2) replenishment
 * cost (if span is fixed and preload count scales with p.n, the
 * per-tick replenish rate scales with n too, so total churn over an
 * n-iteration run scales as n^2). Instead, preload count and span are
 * fixed per regime, reusing this file's own baseline constants: BASE_N
 * preload timers over a BASE_SPAN_HUGE*100 span at the main-baseline
 * scale, BASE_N_HUGE preload timers over a proportionally wider span at
 * the huge-baseline scale, so the preload:span ratio, and therefore the
 * replenish rate, stays identical across regimes. That keeps total
 * replenishment cost linear in p.n (a fixed per-tick rate times an
 * n-iteration loop), the same order as the loop's own foreground churn,
 * at every scale this file tests.
 *
 * Preload timers are replenished, outside the timed window, at the same
 * statistical rate they retire: with TTLs spread roughly uniformly over
 * [0, preload_span], a preload timer's expected lifetime is
 * preload_span/2, so replacing preload_count of them at rate
 * preload_count/(preload_span/2) per tick holds the preload population at
 * a steady state instead of decaying away. Preload-replenish ids and
 * foreground-churn ids live in disjoint ranges (fg_id_base splits them)
 * so the deterministic replenish-rate accounting can never collide with
 * the foreground insert id space. Replenishment TTLs are drawn from the
 * same p.distinct-bucketed value set gen_ttls uses elsewhere, not a raw
 * continuous range: a continuous draw would silently mint up to
 * preload_span distinct TTL values regardless of p.distinct, pushing the
 * distinct-TTL fraction toward 1 and triggering exactly the worst-case
 * crossover behavior the article's evaluation section documents for
 * Lawn2, an artifact of this harness, not of the algorithm.
 *
 * Note "n" therefore means something different here than in every other
 * n-axis figure in this file: it is the number of timed foreground
 * operations sampled, not the size of the population under test (that's
 * fixed per regime, at preload_count). */
typedef struct {
    uint64_t preload_timer_count;
    uint64_t preload_span;
    double preload_replenish_rate;
    uint64_t max_preload_replenish;
    uint64_t fg_id_base;
} lifecycle_math_t;

static lifecycle_math_t calc_lifecycle_math(size_t n, uint64_t preload_n) {
    lifecycle_math_t m;
    m.preload_timer_count = preload_n ? preload_n : (n >= (size_t)(10 * BASE_N) ? (uint64_t)BASE_N_HUGE : (uint64_t)BASE_N);
    m.preload_span = (uint64_t)((double)m.preload_timer_count * ((double)BASE_SPAN_HUGE * 100.0 / (double)BASE_N));
    if (m.preload_span < 1) m.preload_span = 1;
    m.preload_replenish_rate = m.preload_timer_count / (m.preload_span / 2.0);
    m.max_preload_replenish = (uint64_t)(m.preload_replenish_rate * (double)n) + 2;
    m.fg_id_base = m.preload_timer_count + m.max_preload_replenish;
    return m;
}

typedef struct {
    uint64_t *live;
    size_t nlive;
    uint64_t next_preload_id;
    uint64_t next_fg_id;
    double preload_accum;
    size_t replenish_idx;
} lifecycle_run_state_t;

static void setup_lifecycle(scenario_t *sc) {
    lifecycle_math_t m = calc_lifecycle_math(sc->p.n, sc->p.preload_n);
    sc->lc_preload_timer_count = m.preload_timer_count;
    sc->lc_fg_id_base = m.fg_id_base;
    sc->lc_preload_replenish_rate = m.preload_replenish_rate;

    sc->lc_preload_ttls = malloc(sc->lc_preload_timer_count * sizeof(uint64_t));
    gen_ttls(sc->lc_preload_ttls, sc->lc_preload_timer_count, m.preload_span, sc->p.distinct, sc->p.workload, SEED);
    
    sc->lc_preload_distinct = sc->p.distinct > m.preload_span ? m.preload_span : (sc->p.distinct < 1 ? 1 : sc->p.distinct);
    sc->lc_preload_bucket_vals = malloc(sc->lc_preload_distinct * sizeof(uint64_t));
    for (uint64_t i = 0; i < sc->lc_preload_distinct; i++) {
        uint64_t v = (i + 1) * m.preload_span / sc->lc_preload_distinct;
        sc->lc_preload_bucket_vals[i] = v < 1 ? 1 : v;
    }

    uint64_t min_bucket = m.preload_span / (sc->lc_preload_distinct ? sc->lc_preload_distinct : 1);
    sc->lc_stagger_W = min_bucket > 2 ? min_bucket - 1 : 0;
    sc->lc_stagger = sc->lc_stagger_W > 0;

    rng_t r; rng_seed(&r, SEED);
    sc->lc_rolls_op = malloc(sc->p.n * sizeof(double));
    sc->lc_rolls_del_idx = malloc(sc->p.n * sizeof(uint64_t));
    for (size_t i = 0; i < sc->p.n; i++) {
        sc->lc_rolls_op[i] = rng_double(&r);
        sc->lc_rolls_del_idx[i] = rng_u64(&r);
    }

    rng_t preload_r; rng_seed(&preload_r, SEED + 2);
    sc->lc_rolls_replenish = malloc(m.max_preload_replenish * sizeof(uint64_t));
    for (size_t i = 0; i < m.max_preload_replenish; i++) {
        sc->lc_rolls_replenish[i] = sc->lc_preload_bucket_vals[rng_u64(&preload_r) % sc->lc_preload_distinct];
    }
}

static void pre_lifecycle(const cts_vtable *vt, cts_store *s, const scenario_t *sc, void **run_state) {
    lifecycle_run_state_t *rs = malloc(sizeof(*rs));
    *run_state = rs;

    rs->live = malloc((sc->lc_fg_id_base + sc->p.n) * sizeof(uint64_t));
    rs->nlive = 0;
    rs->next_preload_id = sc->lc_preload_timer_count;
    rs->next_fg_id = sc->lc_fg_id_base;
    rs->preload_accum = 0.0;
    rs->replenish_idx = 0;

    /* Staggered preload: insert the background timers at spread arrival times
     * instead of all at t=0. Bulk loading collapses every deadline onto the t
     * distinct TTL values, so a wheel populates only ~t slots (its easy case,
     * and specifically what let a single deadline's slot cascade catastrophic
     * batches of ~N/t timers at once) while Lawn always has t buckets;
     * staggering spreads deadlines across many slots (the realistic steady
     * state a store actually runs under) without changing Lawn's bucket
     * count. Verified this is the right call: bulk-loaded wahern showed
     * artificial multi-millisecond cascade stalls (up to 49ms at N=10M) that
     * vanish under staggering, while low-t points that looked like "no Lawn
     * advantage" under bulk load (deadlines collapsed into few slots) show a
     * real 3-11x Lawn2 win once arrivals are spread. The stagger window stays
     * below the smallest deadline so nothing expires before measuring.
     * Requires vt->advance (a cheap clock fast-forward, implemented by every
     * adapter); falls back to bulk if some future adapter lacks it. */
    int stagger = sc->lc_stagger && (vt->advance != NULL);
    for (size_t i = 0; i < sc->lc_preload_timer_count; i++) {
        if (stagger) {
            vt->advance(s, (uint64_t)(((double)i / (double)sc->lc_preload_timer_count) * (double)sc->lc_stagger_W));
        }
        vt->start(s, i, sc->lc_preload_ttls[i]);
        rs->live[rs->nlive++] = i;
    }
    if (stagger) vt->advance(s, sc->lc_stagger_W);
}

static size_t payload_lifecycle(const cts_vtable *vt, cts_store *s, const scenario_t *sc, void *run_state, double *out) {
    lifecycle_run_state_t *rs = (lifecycle_run_state_t *)run_state;
    size_t c = 0;

    for (size_t i = 0; i < sc->p.n; i++) {
        uint64_t t0 = cts_now_ns();
        if (sc->lc_rolls_op[i] > 0.5 || rs->nlive == 0) {
            vt->start(s, rs->next_fg_id, sc->ttls[rs->next_fg_id - sc->lc_fg_id_base]);
            rs->live[rs->nlive++] = rs->next_fg_id++;
        }
        if (sc->lc_rolls_op[i] < 0.01) {
            size_t idx = (size_t)(sc->lc_rolls_del_idx[i] % rs->nlive);
            vt->stop(s, rs->live[idx]);
            rs->live[idx] = rs->live[--rs->nlive];
        }
        vt->tick(s); 
        out[c++] = (double)(cts_now_ns() - t0);

        rs->preload_accum += sc->lc_preload_replenish_rate;
        while (rs->preload_accum >= 1.0 && rs->next_preload_id < sc->lc_fg_id_base) {
            uint64_t new_ttl = sc->lc_rolls_replenish[rs->replenish_idx++];
            vt->start(s, rs->next_preload_id, new_ttl);
            rs->live[rs->nlive++] = rs->next_preload_id++;
            rs->preload_accum -= 1.0;
        }
    }
    return c;
}

static void post_lifecycle(const cts_vtable *vt, cts_store *s, const scenario_t *sc, void *run_state) {
    (void)vt; (void)s; (void)sc;
    lifecycle_run_state_t *rs = (lifecycle_run_state_t *)run_state;
    free(rs->live);
    free(rs);
}

static void teardown_lifecycle(scenario_t *sc) {
    free(sc->lc_preload_ttls);
    free(sc->lc_preload_bucket_vals);
    free(sc->lc_rolls_op);
    free(sc->lc_rolls_del_idx);
    free(sc->lc_rolls_replenish);
}

/* ---- Configuration Array ---- */
static const op_t OPS[] = {
    {"insert",       MAX_OPS / BATCH + 2, NULL,             pre_insert,    payload_insert,       NULL,             NULL},
    {"delete",       MAX_OPS / BATCH + 2, setup_delete,     pre_start_all, payload_delete,       NULL,             teardown_delete},
    {"tick_advance", TICKS / BATCH + 2,   setup_empty_tick, pre_empty_tick,payload_empty_tick,   post_empty_tick,  NULL},
    {"expiry",       2,                   setup_expiry,     pre_start_all, payload_expiry,       NULL,             NULL},
    {"memory",       1,                   NULL,             NULL,          payload_memory,       NULL,             NULL},
    {"tick_scan",    TICK_SCAN_SAMPLES,   setup_tick_scan,  pre_start_all, payload_tick_scan,    NULL,             teardown_tick_scan},
    {"lifecycle",    OP_PER_N,            setup_lifecycle,  pre_lifecycle, payload_lifecycle,    post_lifecycle,   teardown_lifecycle},
};

/* ---- Utility & Memory Tracking ---- */
static double free_and_inactive_bytes(void) {
#ifdef __APPLE__
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vm_stat;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vm_stat, &count) == KERN_SUCCESS) {
        return (double)(vm_stat.free_count + vm_stat.inactive_count + vm_stat.speculative_count) * (double)sysconf(_SC_PAGESIZE);
    }
    return -1.0;
#elif defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return (double)info.freeram * (double)info.mem_unit; 
    }
    return -1.0;
#else
    return -1.0;
#endif
}


/* Measured at n=1M (results/memory_n.csv, wheel_comparison.csv), not guessed:
 * a safety-margin input for memory_ok(), not a reported figure. */
static double bytes_per_timer_estimate(const char *algo) {
    if (!strcmp(algo, "lawn"))       return 113.0;
    if (!strcmp(algo, "lawn2"))      return 48.0;
    if (!strcmp(algo, "wahern"))     return 88.0;
    if (!strcmp(algo, "naive"))      return 38.0;
    if (!strcmp(algo, "heap"))       return 25.0;
    if (!strcmp(algo, "linuxwheel")) return 32.0;
    return 150.0;
}

static uint64_t next_pow2(uint64_t x) {
    return x <= 1 ? 1 : 1ULL << (64 - __builtin_clzll(x - 1));
}

static int memory_ok(const char *op, const char *algo, size_t n, uint64_t ttl_span, uint64_t preload_n, double *need_gb_out, double *budget_gb_out) {
    double need = (bytes_per_timer_estimate(algo) + 16.0) * (double)n;
    if (!strcmp(op, "lifecycle")) {
        /* Extra memory the "lifecycle" op needs beyond bytes_per_timer_estimate*n:
         * the harness's own id-indexed arrays (sized to cover the preload
         * population, its total replenishment count over the run, and the
         * foreground churn), plus, for naive specifically, its ring growing to
         * cover the preload span itself. That ring growth is not a harness
         * artifact to route around, it is naive's own documented overflow
         * weakness showing up under a genuinely wide, huge-scale TTL span,
         * exactly the scenario this test exists to cover. Mirrors the
         * regime/span/rate math in calc_lifecycle_math. */
        lifecycle_math_t m = calc_lifecycle_math(n, preload_n);
        need = (bytes_per_timer_estimate(algo) + 16.0) * (double)m.preload_timer_count;
        need += (m.fg_id_base + (double)n) * 8.0 * 2.0;   /* harness live[]/ttls[] arrays */
        if (!strcmp(algo, "naive")) need += (double)next_pow2(m.preload_span) * 24.0;
    } else if (!strcmp(algo, "naive")) {
        need += (double)next_pow2(ttl_span) * 24.0;
    }
    double avail = free_and_inactive_bytes();
    double budget = (avail < 0) ? 1e18 : (avail * (MEM_SAFETY_PCT / 100.0));
    *need_gb_out = need / 1e9;
    *budget_gb_out = budget / 1e9;
    return need <= budget;
}

static void human_readable_n(size_t n, char* n_str) {
    if (n >= (1000 * 1000)) snprintf(n_str, 32, "%zuM", (n / (1000 * 1000)));
    else if (n > 1000)      snprintf(n_str, 32, "%zuK", (n / 1000));
    else                    snprintf(n_str, 32, "%zu", n);
}

/* ---- Core Execution & Aggregation ---- */
/* Runs every warmup+measured repetition of one (op, algo, scenario) point in
 * a forked child, aggregates there, and ships back only the fixed-size
 * agg_t over shared memory: the child's exit code can't carry a sample
 * count instead (POSIX exit codes are 8 bits; an unbatched op like
 * "lifecycle" produces thousands of samples per run and would wrap mod
 * 256), and forking gives every point a clean heap the OS reclaims when the
 * child exits, instead of accumulating fragmentation across points in the
 * parent. */
static agg_t run_point(const cts_vtable *vt, const op_t *op, const scenario_t *sc) {
    int runs = sc->p.n >= (10 * BASE_N) ? 3 : 7;
    int warmup = sc->p.n >= (10 * BASE_N) ? 1 : 2;
    size_t per_run_max = (op->per_run_max == (size_t)OP_PER_N) ? sc->p.n : op->per_run_max;
    size_t total_max = per_run_max * (size_t)runs;

    void *shm = mmap(NULL, sizeof(agg_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    agg_t *out_agg = (agg_t *)shm;

    pid_t pid = fork();
    if (pid == 0) {
        /* CHILD PROCESS: runs every repetition, aggregates, exits immediately
         * to release all heap memory to the OS. */
        double *all_samples = malloc(total_max * sizeof(double));
        double *tmp = malloc(per_run_max * sizeof(double));
        size_t total = 0;
        
        for (int r = 0; r < warmup + runs; r++) {
            cts_store *s = vt->create();
            void *state = NULL;
            
            if (op->pre) op->pre(vt, s, sc, &state);
            size_t c = op->payload(vt, s, sc, state, tmp);
            if (op->post) op->post(vt, s, sc, state);
            vt->destroy(s);

            if (r >= warmup) {
                memcpy(all_samples + total, tmp, c * sizeof(double));
                total += c;
            }
        }
        
        *out_agg = aggregate(all_samples, total);
        free(tmp);
        free(all_samples);
        _exit(0);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    
    agg_t a = {0};
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        a = *out_agg;
    }
    
    munmap(shm, sizeof(agg_t));
    return a;
}

typedef struct {
    agg_t agg;
    int success;
    double need_gb;
    double budget_gb;
} exec_res_t;

static void execute_shared_scenario(
    const op_t *op, params_t p, const cts_vtable *const *algos, int num_algos, exec_res_t *results) {
    int any_success = 0;
    for (int i = 0; i < num_algos; i++) {
        results[i].success = memory_ok(op->name, algos[i]->name, p.n, p.ttl_span, p.preload_n, 
                                       &results[i].need_gb, &results[i].budget_gb);
        if (results[i].success) any_success = 1;
    }

    if (!any_success) return; 

    scenario_t sc = { .p = p };
    sc.ttls = malloc(p.n * sizeof(*sc.ttls));
    gen_ttls(sc.ttls, p.n, p.ttl_span, p.distinct, p.workload, SEED);
    
    if (op->setup) op->setup(&sc);

    for (int i = 0; i < num_algos; i++) {
        if (results[i].success) {
            results[i].agg = run_point(algos[i], op, &sc);
        }
    }

    if (op->teardown) op->teardown(&sc);
    free(sc.ttls);
}

static void sweep_axis(const op_t *op, const char *axis, const char *dir, bool huge) {
    char path[512];
    if (!huge) snprintf(path, sizeof path, "%s/%s_%s.csv", dir, op->name, axis);
    else snprintf(path, sizeof path, "%s/%s_%s_huge.csv", dir, op->name, axis);
    FILE *f = fopen(path, "w");
    int is_mem = (strcmp(op->name, "memory") == 0);
    const char *u = is_mem ? "bytes" : "ns";
    fprintf(f, "%s,algo,mean_%s,std_%s,p99_%s,max_%s,n_samples,runs,warmup,seed\n", axis, u, u, u, u);
    FILE *pf = NULL;
    if (is_mem && strcmp(axis, "n") == 0) {
        char pp[512];
        if (!huge) snprintf(pp, sizeof pp, "%s/memory_per_timer_n.csv", dir);
        else snprintf(pp, sizeof pp, "%s/memory_per_timer_n_huge.csv", dir);
        pf = fopen(pp, "w");
        fprintf(pf, "n,algo,mean_bytes,std_bytes,p99_bytes,max_bytes,n_samples,runs,warmup,seed\n");
    }
    params_t base_params = {BASE_N, BASE_SPAN, 100, WL_UNIFORM, 0};
    if (huge) { base_params.n = BASE_N_HUGE; base_params.ttl_span = BASE_SPAN_HUGE; }
    size_t nvals;
    if      (!strcmp(axis, "n"))             nvals = huge ? GET_SIZE(N_VALS_HUGE) : GET_SIZE(N_VALS);
    else if (!strcmp(axis, "ttl_span"))      nvals = GET_SIZE(SPAN_VALS);
    else if (!strcmp(axis, "distinct_ttls")) nvals = GET_SIZE(DISTINCT_TTL_VALS);
    else if (!strcmp(axis, "workload"))      nvals = GET_SIZE(WORKLOAD_VALS);
    else {
        printf("no values defined for axis \"%s\"\n", axis);
        if (pf) fclose(pf);
        fclose(f);
        return;
    }
    for (size_t vi = 0; vi < nvals; vi++) {
        params_t p = base_params;
        char valstr[32];
        if (!strcmp(axis, "n")) {
            size_t pop = huge ? (size_t)(1000 * 1000 * N_VALS_HUGE[vi]) : (size_t)(1000 * N_VALS[vi]);
            if (!strcmp(op->name, "lifecycle")) {
                p.preload_n = pop;
                p.n = FG_OPS;
                p.ttl_span = BASE_SPAN_HUGE;
            } else { p.n = pop; }
            snprintf(valstr, 32, "%zu", pop);
        }
        else if (!strcmp(axis, "ttl_span")) { p.ttl_span = SPAN_VALS[vi]; snprintf(valstr, 32, "%llu", (unsigned long long)p.ttl_span); }
        else if (!strcmp(axis, "distinct_ttls")){ p.distinct = DISTINCT_TTL_VALS[vi]; snprintf(valstr, 32, "%llu", (unsigned long long)p.distinct); }
        else if (!strcmp(axis, "workload")){ p.workload = WORKLOAD_VALS[vi]; snprintf(valstr, 32, "%s", WORKLOAD_NAMES[vi]); }
        char n_str[32]; human_readable_n(p.n, n_str);
        exec_res_t results[cts_nalgos];
        execute_shared_scenario(op, p, cts_algos, cts_nalgos, results);
        int runs = p.n >= (10 * BASE_N) ? 3 : 7;
        int warmup = p.n >= (10 * BASE_N) ? 1 : 2;
        for (int a = 0; a < cts_nalgos; a++) {
            if (results[a].success) {
                fprintf(f, "%s,%s,%.4f,%.4f,%.4f,%.4f,%zu,%d,%d,%d\n", valstr, cts_algos[a]->name, 
                        results[a].agg.mean, results[a].agg.std, results[a].agg.p99, results[a].agg.max, 
                        results[a].agg.n, runs, warmup, SEED);
                if (pf) {
                    fprintf(pf, "%s,%s,%.4f,%.4f,%.4f,%.4f,%zu,%d,%d,%d\n", valstr, cts_algos[a]->name, 
                            results[a].agg.mean / (double)p.n, results[a].agg.std / (double)p.n, 
                            results[a].agg.p99 / (double)p.n, results[a].agg.max / (double)p.n, 
                            results[a].agg.n, runs, warmup, SEED);
                }
                printf("DONE %-8s %-13s axis=%-13s value=%s n=%s\n", op->name, cts_algos[a]->name, axis, valstr, n_str);
            } else {
                printf("SKIP %-8s %-13s axis=%-13s value=%s n=%s (needs ~%.1fGB, safe budget ~%.1fGB)\n",
                    op->name, cts_algos[a]->name, axis, valstr, n_str, results[a].need_gb, results[a].budget_gb);
            }
        }
        if (pf) fflush(pf);
        fflush(f);
    }
    fclose(f);
    if (pf) fclose(pf);
    printf("  wrote %s\n", path);
}

static const op_t* op_from_name(const char *s) {
    const int num_ops = GET_SIZE(OPS);
    for (int i = 0; i < num_ops; i++) if (!strcmp(OPS[i].name, s)) return &OPS[i];
    return NULL;
}

/* ---- Inflection Drivers (Flattened & Index-Mapped) ---- */
typedef struct { double lawn, lawn2, wahern, naive; } tick_metrics_t;
typedef struct { double mean, p99, max; } life_metric_t;
typedef struct { life_metric_t lawn2, wahern; } life_metrics_t;

static int lawn_idx = -1, lawn2_idx = -1, wahern_idx = -1, naive_idx = -1;

static void init_algo_indices(void) {
    for (int i = 0; i < cts_nalgos; i++) {
        if (!strcmp(cts_algos[i]->name, "lawn")) lawn_idx = i;
        else if (!strcmp(cts_algos[i]->name, "lawn2")) lawn2_idx = i;
        else if (!strcmp(cts_algos[i]->name, "wahern")) wahern_idx = i;
        else if (!strcmp(cts_algos[i]->name, "naive")) naive_idx = i;
    }
}

static tick_metrics_t measure_inflection_tick(size_t n, uint64_t t, uint64_t span) {
    tick_metrics_t m = {0};
    params_t p = {n, span, t, WL_UNIFORM, 0};
    exec_res_t res[cts_nalgos];
    execute_shared_scenario(op_from_name("tick_scan"), p, cts_algos, cts_nalgos, res);
    
    m.lawn   = (lawn_idx >= 0 && res[lawn_idx].success) ? res[lawn_idx].agg.mean : 0;
    m.lawn2  = (lawn2_idx >= 0 && res[lawn2_idx].success) ? res[lawn2_idx].agg.mean : 0;
    m.wahern = (wahern_idx >= 0 && res[wahern_idx].success) ? res[wahern_idx].agg.mean : 0;
    m.naive  = (naive_idx >= 0 && res[naive_idx].success) ? res[naive_idx].agg.mean : 0;
    return m;
}

static life_metrics_t measure_inflection_life(size_t n, uint64_t t) {
    life_metrics_t m = {0};
    params_t p = {FG_OPS, BASE_SPAN_HUGE, t, WL_UNIFORM, n};
    exec_res_t res[cts_nalgos];
    execute_shared_scenario(op_from_name("lifecycle"), p, cts_algos, cts_nalgos, res);
    
    if (lawn2_idx >= 0 && res[lawn2_idx].success) {
        m.lawn2.mean = res[lawn2_idx].agg.mean; m.lawn2.p99 = res[lawn2_idx].agg.p99; m.lawn2.max = res[lawn2_idx].agg.max;
    }
    if (wahern_idx >= 0 && res[wahern_idx].success) {
        m.wahern.mean = res[wahern_idx].agg.mean; m.wahern.p99 = res[wahern_idx].agg.p99; m.wahern.max = res[wahern_idx].agg.max;
    }
    return m;
}

static void sweep_inflection_curve(FILE *f, const char *regime_name, size_t n, uint64_t span) {
    static const uint64_t TS[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000};
    double prev_ratio2 = 0, prev_t = 0, xover2 = 0, prev_ratio_life = 0, xover_life = 0;
    
    for (size_t ti = 0; ti < GET_SIZE(TS); ti++) {
        uint64_t t = TS[ti];
        if (t > n || t > 10000) continue;
        
        tick_metrics_t tm = measure_inflection_tick(n, t, span);
        life_metrics_t lm = measure_inflection_life(n, t); 
        
        double ratio_tick  = tm.wahern > 0 ? tm.lawn / tm.wahern : 0;
        double ratio2_tick = tm.wahern > 0 ? tm.lawn2 / tm.wahern : 0;
        double ratio_life  = lm.lawn2.mean > 0 ? lm.wahern.mean / lm.lawn2.mean : 0;
        double ratio_p99   = lm.lawn2.p99 > 0 ? lm.wahern.p99 / lm.lawn2.p99 : 0;

        fprintf(f, "%s,%zu,%llu,%llu,%.6g,%.2f,%.2f,%.2f,%.2f,%.4f,%.4f,%.2f,%.2f,%.4f,%.2f,%.2f,%.4f,%.2f,%.2f\n",
                regime_name, n, (unsigned long long)t, (unsigned long long)span, (double)t / n, 
                tm.lawn, tm.lawn2, tm.wahern, tm.naive, ratio_tick, ratio2_tick, 
                lm.lawn2.mean, lm.wahern.mean, ratio_life, lm.lawn2.p99, lm.wahern.p99, ratio_p99, lm.lawn2.max, lm.wahern.max);
        
        if (ratio_life > 0 && prev_ratio_life > 0 && !xover_life && ((prev_ratio_life - 1.0) * (ratio_life - 1.0) < 0)) {
            xover_life = exp(log(prev_t) + ((0.0 - log(prev_ratio_life)) / (log(ratio_life) - log(prev_ratio_life))) * (log((double)t) - log(prev_t)));
        }
        if (ratio_life > 0) prev_ratio_life = ratio_life;
        
        if (prev_t > 0 && !xover2 && ((prev_ratio2 - 1.0) * (ratio2_tick - 1.0) < 0)) {
            xover2 = exp(log(prev_t) + ((0.0 - log(prev_ratio2)) / (log(ratio2_tick) - log(prev_ratio2))) * (log((double)t) - log(prev_t)));
        }
        
        printf("    %s regime (span=%llu): distinct TTLs=%llu\n", regime_name, (unsigned long long)span, (unsigned long long)t);
        prev_ratio2 = ratio2_tick; prev_t = (double)t;
        fflush(f);
    }
    
    if (xover2) printf("  N=%zu (%s): lawn2 tick_scan crossover t*=%.0f (t/N=%.2e)\n", n, regime_name, xover2, xover2 / n);
    else printf("  N=%zu (%s): lawn2 tick_scan %s wahern across the whole range\n", n, regime_name, prev_ratio2 < 1 ? "beats" : "loses to");
    
    if (xover_life) printf("  N=%zu (%s): lawn2 lifecycle crossover t*=%.0f (t/N=%.2e)\n", n, regime_name, xover_life, xover_life / n);
    else printf("  N=%zu (%s): lawn2 lifecycle %s wahern across the whole range\n", n, regime_name, prev_ratio_life > 1 ? "beats" : "loses to");
}

/* Two distinct, both-legitimate span regimes, one merged CSV distinguished by
 * span_regime:
 *  - scaled: ttl_span scales with n at the same span-per-timer ratio the
 *    lifecycle op uses for its preload population (BASE_SPAN_HUGE*100/BASE_N),
 *    holding timer density per tick-slot constant across n. Isolates the
 *    O(t) bucket-scan crossover cleanly. This is the regime
 *    article/src/make_figures.py's inflection_plot() filters to.
 *  - fixed: ttl_span stays at 10000 regardless of n, so density (n/ttl_span)
 *    grows unbounded as n grows. At large n this is dense enough to trigger
 *    wahern's own cascade pathology, its own legitimate finding (an
 *    overflow-under-fixed-span effect), but it masks the O(t) crossover, so
 *    it's a separate span_regime value, not conflated with "scaled". */
static void run_inflection(const char *dir) {
    init_algo_indices();
    static const size_t NS[] = {1000, 10000, 100000, 1000000, 10000000, 100000000};
    char path[512];
    snprintf(path, sizeof path, "%s/inflection.csv", dir);
    FILE *f = fopen(path, "w");
    
    fprintf(f, "span_regime,N,t,ttl_span,t_over_N,lawn_pertick_ns,lawn2_pertick_ns,wahern_pertick_ns,naive_pertick_ns,"
               "ratio_lawn_over_wahern,ratio_lawn2_over_wahern,"
               "lawn2_life_ns,wahern_life_ns,ratio_wahern_over_lawn2_life,"
               "lawn2_life_p99,wahern_life_p99,ratio_wahern_over_lawn2_life_p99,"
               "lawn2_life_max,wahern_life_max\n");

    printf("inflection (tick_scan + lifecycle vs wahern; crossover reported for lawn2):\n");
    
    for (size_t ni = 0; ni < GET_SIZE(NS); ni++) {
        size_t n = NS[ni];
        sweep_inflection_curve(f, "fixed", n, 10000);
        
        uint64_t scaled_span = (uint64_t)((double)n * ((double)BASE_SPAN_HUGE * 100.0 / (double)BASE_N));
        if (scaled_span < 1) scaled_span = 1;
        
        if (scaled_span != 10000) {
            sweep_inflection_curve(f, "scaled", n, scaled_span);
        }
    }
    fclose(f);
    printf("  wrote %s\n", path);
}

/* ---- Distribution Driver ---- */
static void run_distribution(const char *dir) {
    printf("Benchmarking across distinct TTL distribuitions: Fixed t=1, Discrete t=10, Continuous t=N:\n");
    char path[512];
    snprintf(path, sizeof path, "%s/ttl_distribution.csv", dir);
    FILE *f = fopen(path, "w");
    fprintf(f, "Distribution,algo,N,ttl span,distinct ttls,insert,expiry\n");

    const size_t ttl_count = 100000;

    typedef struct {
        const char *name;
        uint64_t ttl_span;
        uint64_t distinct;
    } dist_profile_t;

    dist_profile_t profiles[] = {
        {"fixed (t=1)",      100,       1},
        {"discrete (t=10)",  100,       10},
        {"continuous (t=N)", ttl_count, ttl_count}
    };

    for (size_t pi = 0; pi < GET_SIZE(profiles); pi++) {
        dist_profile_t prof = profiles[pi];
        printf("--- %s ---\n", prof.name);

        params_t p = {ttl_count, prof.ttl_span, prof.distinct, WL_UNIFORM, 0};
        exec_res_t ins_res[cts_nalgos], exp_res[cts_nalgos];
        execute_shared_scenario(op_from_name("insert"), p, cts_algos, cts_nalgos, ins_res);
        execute_shared_scenario(op_from_name("expiry"), p, cts_algos, cts_nalgos, exp_res);

        for (int a = 0; a < cts_nalgos; a++) {
            double ins_mean = ins_res[a].success ? ins_res[a].agg.mean : 0.0;
            double exp_mean = exp_res[a].success ? exp_res[a].agg.mean : 0.0;
            printf("  %-8s insert: %.2f ns/op | expiry: %.2f ns/op\n", cts_algos[a]->name, ins_mean, exp_mean);
            fprintf(f, "%s,%s,%zu,%llu,%llu,%.2f ns/op,%.2f ns/op\n", prof.name, cts_algos[a]->name, ttl_count, prof.ttl_span, prof.distinct, ins_mean, exp_mean);
        }
    }
    fclose(f);
    printf("wrote %s\n", path);
}

/* ---- Entry Point & Single Driver ---- */
static int wl_from_name(const char *s) {
    if (!strcmp(s, "uniform")) return WL_UNIFORM;
    if (!strcmp(s, "bursty"))  return WL_BURSTY;
    if (!strcmp(s, "spread"))  return WL_SPREAD;
    return -1;
}

static const cts_vtable* algo_from_name(const char *s) {
    for (int i = 0; i < cts_nalgos; i++) if (!strcmp(cts_algos[i]->name, s)) return cts_algos[i];
    return NULL;
}

int run_single(int argc, char **argv) {
    if (argc != 9 && argc != 10 && argc != 11) {
        fprintf(stderr, "usage: %s single <op> <algo> <axis_label> <n> <ttl_span> <distinct> <workload> [safety_pct] [preload_n]\n", argv[0]);
        return 2;
    }
    const char *opname = argv[2], *algoname = argv[3], *axis = argv[4];
    size_t n = (size_t)strtoull(argv[5], NULL, 10);
    uint64_t ttl_span = strtoull(argv[6], NULL, 10);
    uint64_t distinct = strtoull(argv[7], NULL, 10);
    int workload = wl_from_name(argv[8]);
    if (workload < 0) return 2;
    double safety_pct = argc >= 10 ? strtod(argv[9], NULL) : MEM_SAFETY_PCT;
    uint64_t preload_n = argc == 11 ? strtoull(argv[10], NULL, 10) : 0;

    const cts_vtable *algo = algo_from_name(algoname);
    if (!algo) return 2; 

    const op_t *op = op_from_name(opname);
    if (!op) return 2; 

    params_t params = {n, ttl_span, distinct, workload, preload_n};
    const cts_vtable *algos[1] = { algo };
    exec_res_t res[1];
    
    uint64_t t0 = cts_now_ns();
    execute_shared_scenario(op, params, algos, 1, res);

    char n_str[32]; human_readable_n(n, n_str);
    if (res[0].success) {
        double secs = (double)(cts_now_ns() - t0) / 1e9;
        int runs = n >= (10 * BASE_N) ? 3 : 7;
        int warmup = n >= (10 * BASE_N) ? 1 : 2;
        printf("DONE algo %s\noperation %s\nn %s\nttl_span %llu\ndistinct %llu\nworkload %s\nmean %.4f\nstd %.4f\np99 %.4f\nmax %.4f\ntotal runtime %.1fs\nruns %d\nwarmup %d\nseed %d\n",
            algo->name, op->name, n_str, ttl_span, distinct, argv[8], res[0].agg.mean, res[0].agg.std, res[0].agg.p99, res[0].agg.max, secs, runs, warmup, SEED);
    } else {
        printf("SKIP %-8s %-13s axis=%-13s n=%s (needs ~%.1fGB, safe budget ~%.1fGB at %.0f%%)\n",
            op->name, algo->name, axis, n_str, res[0].need_gb, res[0].budget_gb, safety_pct);
        return 1;
    }
    return 0;
}

static void run_sweeps(const char *dir, bool huge) {
    const int num_ops = GET_SIZE(OPS);
    const int num_axes = GET_SIZE(PARAMETER_NAMES);
    printf("C benchmark suite | algos:");
    for (int a = 0; a < cts_nalgos; a++) printf(" %s", cts_algos[a]->name);
    printf(" -> %s\n", dir);
    for (int operation = 0; operation < num_ops; operation++) 
        for (int parameter = 0; parameter < num_axes; parameter++) { 
            printf("  %s vs %s ...\n", OPS[operation].name, PARAMETER_NAMES[parameter]); fflush(stdout);
            sweep_axis(&OPS[operation], PARAMETER_NAMES[parameter], dir, huge);
        }
}

int main(int argc, char **argv) {
    const char *dir = "results";
    char cmd[256]; snprintf(cmd, sizeof cmd, "mkdir -p %s", dir); if (system(cmd)) {}
    
    if (argc > 1) {
        if (!strcmp(argv[1], "sweeps")) { run_sweeps(dir, false); }
        else if (!strcmp(argv[1], "dist")) { run_distribution(dir); }
        else if (!strcmp(argv[1], "inflection")) { run_inflection(dir); }
        else if (!strcmp(argv[1], "huge")) { run_sweeps(dir, true); }
        else if (!strcmp(argv[1], "single")) { return run_single(argc, argv); }
        else if (!strcmp(argv[1], "sweep-op")) {
            if (argc < 4) return 2;
            const op_t *op = op_from_name(argv[2]);
            if (!op) return 2;
            bool huge = (argc >= 5 && !strcmp(argv[4], "huge"));
            sweep_axis(op, argv[3], dir, huge);
        }
        else if (!strcmp(argv[1], "all")) {
            run_sweeps(dir, false); run_sweeps(dir, true);
            run_distribution(dir);
            run_inflection(dir);
        }
        else { printf("unrecognized option %s\n", argv[1]); return 1; }
    } else {
        run_sweeps(dir, false);
        run_distribution(dir);
    }
    printf("done.\n");
    return 0;
}