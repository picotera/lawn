/* C benchmark harness: mirrors src/benchmarks/python. Logical clock, one
 * function per operation, sweeps 4 axes + inflection, emits CSVs matching the
 * Python schema so the same plotting renders them. */
#include "cts.h"
#include "util.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <malloc/malloc.h>   /* macOS: malloc_zone_statistics */

#define SEED   1234

#define MEM_SAFETY_PCT 90.0

#define MAX_OPS 20000
#define OP_PER_N -1

#define TICKS   2000
#define WINDOW  200
#define BATCH   256   /* micro-batch: time B ops per cts_now_ns pair, one
                         (batch_time/B) sample, to clear the ~41ns clock floor */
#define TICK_SCAN_SAMPLES (4 * BATCH)  /* target number of scan-triggering
                                         ticks measure_tick samples per call:
                                         same clock-floor margin BATCH
                                         already establishes, shrunk when the
                                         population has fewer distinct live
                                         bucket values than that */

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
    /* Only measure_lifecycle reads this. 0 (the default for every existing
     * literal params_t{} initializer, unspecified trailing members are
     * zero-initialized) means "use the regime-based default"; nonzero
     * overrides preload_timer_count directly, decoupling population size
     * from the fixed BASE_N/BASE_N_HUGE regime switch. */
    uint64_t preload_n;
} params_t;

static const char *PARAMETER_NAMES[] = {"n", "ttl_span", "distinct_ttls", "workload"};

#define BASE_PARAMS {BASE_N, BASE_SPAN, 100, WL_UNIFORM}
#define GET_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

typedef size_t (*measure_fn)(const cts_vtable *, params_t, double *);

/* ---- measurements (one per operation) ---- */

static size_t measure_insert(const cts_vtable *vt, params_t p, double *out) {
    uint64_t *ttls = malloc(p.n * sizeof *ttls);
    gen_ttls(ttls, p.n, p.ttl_span, p.distinct, p.workload, SEED);
    cts_store *s = vt->create();
    size_t timed = p.n < MAX_OPS ? p.n : MAX_OPS, pre = p.n - timed, c = 0;
    for (size_t i = 0; i < pre; i++) vt->start(s, i, ttls[i]);
    for (size_t i = pre; i < p.n; ) {
        size_t bs = (p.n - i) < BATCH ? (p.n - i) : BATCH;
        uint64_t t0 = cts_now_ns();
        for (size_t k = 0; k < bs; k++) vt->start(s, i + k, ttls[i + k]);
        out[c++] = (double)(cts_now_ns() - t0) / (double)bs;
        i += bs;
    }
    vt->destroy(s); free(ttls);
    return c;
}

static size_t measure_delete(const cts_vtable *vt, params_t p, double *out) {
    uint64_t *ttls = malloc(p.n * sizeof *ttls);
    gen_ttls(ttls, p.n, p.ttl_span, p.distinct, p.workload, SEED);
    cts_store *s = vt->create();
    for (size_t i = 0; i < p.n; i++) vt->start(s, i, ttls[i]);
    uint64_t *perm = malloc(p.n * sizeof *perm);
    gen_shuffle(perm, p.n, SEED);
    size_t timed = p.n < MAX_OPS ? p.n : MAX_OPS, c = 0;
    for (size_t k = 0; k < timed; ) {
        size_t bs = (timed - k) < BATCH ? (timed - k) : BATCH;
        uint64_t t0 = cts_now_ns();
        for (size_t j = 0; j < bs; j++) vt->stop(s, perm[k + j]);
        out[c++] = (double)(cts_now_ns() - t0) / (double)bs;
        k += bs;
    }
    
    for (size_t k = timed; k < p.n; k++) {
        vt->stop(s, perm[k]);
    }

    vt->destroy(s);
    free(ttls); 
    free(perm);

    return c;
}

/* Pure idle-tick (guard-hit) cost: every timer's TTL is shifted past the
 * whole measured window, so nothing is ever due and every tick() call must
 * take the O(1) empty-tick fast path, for every algorithm, not just the
 * ones with an explicit "closest expiration" guard. Verified, not assumed:
 * asserts vt->size() never changes across the whole window, so a bug that
 * fired something early would fail loudly here instead of silently
 * contaminating the measurement. */
static size_t measure_empty_tick(const cts_vtable *vt, params_t p, double *out) {
    uint64_t *ttls = malloc(p.n * sizeof *ttls);
    gen_ttls(ttls, p.n, p.ttl_span, p.distinct, p.workload, SEED);
    cts_store *s = vt->create();
    for (size_t i = 0; i < p.n; i++) vt->start(s, i, ttls[i] + TICKS); /* nothing due in window */
    uint64_t before_size = vt->size(s);
    size_t c = 0;
    for (int t = 0; t < TICKS; ) {
        int bs = (TICKS - t) < BATCH ? (TICKS - t) : BATCH;
        uint64_t t0 = cts_now_ns();
        for (int j = 0; j < bs; j++) vt->tick(s);
        out[c++] = (double)(cts_now_ns() - t0) / (double)bs;
        t += bs;
    }
    if (vt->size(s) != before_size) {
        fprintf(stderr, "measure_empty_tick: %s fired timers during a window "
                        "that should have had none due (size %llu -> %llu)\n",
                        vt->name, (unsigned long long)before_size,
                        (unsigned long long)vt->size(s));
        abort();
    }
    vt->destroy(s);
    free(ttls);
    return c;
}

static size_t measure_expiry(const cts_vtable *vt, params_t p, double *out) {
    uint64_t *ttls = malloc(p.n * sizeof *ttls);
    gen_ttls(ttls, p.n, WINDOW, p.distinct, p.workload, SEED); /* all due within WINDOW */
    cts_store *s = vt->create();
    for (size_t i = 0; i < p.n; i++) vt->start(s, i, ttls[i]);
    size_t c = 0;
    for (int t = 0; t < WINDOW; ) {
        int bs = (WINDOW - t) < BATCH ? (WINDOW - t) : BATCH;  /* WINDOW<BATCH: one batch */
        uint64_t t0 = cts_now_ns();
        for (int j = 0; j < bs; j++) vt->tick(s);
        out[c++] = (double)(cts_now_ns() - t0) / (double)bs;
        t += bs;
    }
    vt->destroy(s); free(ttls);
    return c;
}

static size_t measure_memory(const cts_vtable *vt, params_t p, double *out) {
    uint64_t *ttls = malloc(p.n * sizeof *ttls);           /* before snapshot: not counted */
    gen_ttls(ttls, p.n, p.ttl_span, p.distinct, p.workload, SEED);
    malloc_statistics_t before, after;
    malloc_zone_statistics(malloc_default_zone(), &before);
    cts_store *s = vt->create();
    for (size_t i = 0; i < p.n; i++) vt->start(s, i, ttls[i]);
    malloc_zone_statistics(malloc_default_zone(), &after);
    double bytes = (double)after.size_in_use - (double)before.size_in_use;
    if (bytes < 0) bytes = 0;
    vt->destroy(s); free(ttls);
    out[0] = bytes;
    return 1;
}

/* Realistic-mix stress test: pre-fills a realistic, fixed-size preload
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
 * crossover behavior Section~\ref{sec:eval} documents for Lawn2, an
 * artifact of this harness, not of the algorithm.
 *
 * Note "n" therefore means something different here than in every other
 * n-axis figure in this file: it is the number of timed foreground
 * operations sampled, not the size of the population under test (that's
 * fixed per regime, at preload_count). */
static size_t measure_lifecycle(const cts_vtable *vt, params_t p, double *out) {
    uint64_t preload_timer_count;
    if (p.preload_n) {
        preload_timer_count = p.preload_n;
    } else {
        int huge_regime = p.n >= (size_t)(10 * BASE_N);
        preload_timer_count = huge_regime ? (uint64_t)BASE_N_HUGE : (uint64_t)BASE_N;
    }
    /* Same preload:span ratio in both regimes, so replenish rate (and
     * thus total replenishment cost) doesn't grow with preload size. */
    double preload_span_per_timer = (double)BASE_SPAN_HUGE * 100.0 / (double)BASE_N;
    uint64_t preload_span = (uint64_t)(preload_timer_count * preload_span_per_timer);
    if (preload_span < 1) preload_span = 1;

    double preload_avg_lifetime = preload_span / 2.0;
    double preload_replenish_rate = preload_timer_count / preload_avg_lifetime;   /* per tick */
    uint64_t max_preload_replenish = (uint64_t)(preload_replenish_rate * (double)p.n) + 2;

    uint64_t *preload_ttls = malloc(preload_timer_count * sizeof *preload_ttls);
    gen_ttls(preload_ttls, preload_timer_count, preload_span, p.distinct, p.workload, SEED);

    /* Bucket set for replenishment TTLs: same formula gen_ttls uses, so
     * replenishment respects p.distinct instead of minting new values. */
    uint64_t preload_distinct = p.distinct > preload_span ? preload_span : (p.distinct < 1 ? 1 : p.distinct);
    uint64_t *preload_bucket_vals = malloc(preload_distinct * sizeof *preload_bucket_vals);
    for (uint64_t i = 0; i < preload_distinct; i++) {
        uint64_t v = (i + 1) * preload_span / preload_distinct;
        preload_bucket_vals[i] = v < 1 ? 1 : v;
    }

    uint64_t fg_id_base = preload_timer_count + max_preload_replenish;
    uint64_t *ttls = malloc(p.n * sizeof *ttls);
    gen_ttls(ttls, p.n, p.ttl_span, p.distinct, p.workload, SEED + 1);
    cts_store *s = vt->create();

    uint64_t *live = malloc((fg_id_base + p.n) * sizeof *live);
    size_t nlive = 0;
    for (size_t i = 0; i < preload_timer_count; i++) { vt->start(s, i, preload_ttls[i]); live[nlive++] = i; }

    rng_t r; rng_seed(&r, SEED);
    rng_t preload_r; rng_seed(&preload_r, SEED + 2);
    uint64_t next_preload_id = preload_timer_count;
    uint64_t next_fg_id = fg_id_base;
    double preload_accum = 0.0;
    size_t c = 0;
    for (size_t i = 0; i < p.n; i++) {
        double roll = rng_double(&r);
        uint64_t t0 = cts_now_ns();
        if (roll > 0.5 || nlive == 0) { // 50% chance to trigger an insert op
            vt->start(s, next_fg_id, ttls[next_fg_id - fg_id_base]);
            live[nlive++] = next_fg_id++;
        }
        if (roll < 0.01) { // 1% chance to trigger a delete op on a live timer
            size_t idx = (size_t)(rng_u64(&r) % nlive);
            vt->stop(s, live[idx]);
            live[idx] = live[--nlive];
        }
        vt->tick(s); // in any case tick to drain
        out[c++] = (double)(cts_now_ns() - t0);

        /* Untimed: replenish preload timers at their statistical
         * retirement rate, so the population stays steady instead of
         * decaying. */
        preload_accum += preload_replenish_rate;
        while (preload_accum >= 1.0 && next_preload_id < fg_id_base) {
            uint64_t new_ttl = preload_bucket_vals[rng_u64(&preload_r) % preload_distinct];
            vt->start(s, next_preload_id, new_ttl);
            live[nlive++] = next_preload_id++;
            preload_accum -= 1.0;
        }
    }
    free(preload_bucket_vals);

    vt->destroy(s);
    free(preload_ttls);
    free(ttls);
    free(live);
    return c;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

/* Reports the cost of a tick that actually does a scan (crosses a live
 * bucket's expiry), not an idle guard-hit tick (measure_empty_tick already
 * covers that). gen_ttls is a deterministic function of
 * (n, ttl_span, distinct, workload, SEED), so the exact set of populated
 * bucket values is just the sorted, deduplicated ttls[] array this
 * function already generates, no need to guess or re-derive gen_ttls'
 * bucket formula. For each populated value, fast-forward (untimed) to one
 * tick before it, cheap regardless of how far out it sits since every tick
 * along the way is a genuine empty tick, then time only the single tick
 * that crosses the boundary. Samples up to TICK_SCAN_SAMPLES such boundary
 * ticks (fewer if the population has fewer distinct live values than
 * that), so runtime stays bounded regardless of ttl_span or how sparse the
 * boundaries are, unlike timing a full drain. */
static size_t measure_tick(const cts_vtable *vt, params_t p, double *out) {
    uint64_t *ttls = malloc(p.n * sizeof *ttls);
    gen_ttls(ttls, p.n, p.ttl_span, p.distinct, p.workload, SEED);

    uint64_t *sorted = malloc(p.n * sizeof *sorted);
    memcpy(sorted, ttls, p.n * sizeof *sorted);
    qsort(sorted, p.n, sizeof *sorted, cmp_u64);
    size_t nvals = 0;
    for (size_t i = 0; i < p.n; i++)
        if (i == 0 || sorted[i] != sorted[i - 1]) sorted[nvals++] = sorted[i];

    cts_store *s = vt->create();
    for (size_t i = 0; i < p.n; i++) vt->start(s, i, ttls[i]);

    size_t nsamp = nvals < TICK_SCAN_SAMPLES ? nvals : TICK_SCAN_SAMPLES;
    uint64_t current = 0;
    size_t c = 0;
    for (size_t i = 0; i < nsamp; i++) {
        uint64_t target = sorted[i];
        while (current < target - 1) { vt->tick(s); current++; }
        uint64_t t0 = cts_now_ns();
        vt->tick(s);
        current++;
        out[c++] = (double)(cts_now_ns() - t0);
    }

    vt->destroy(s); free(ttls); free(sorted);
    return c;
}

/* ---- aggregation over runs (warmup discarded) ---- */

static int runs_for(size_t n) { return n >= (10 * BASE_N) ? 3 : 7; }
static int warmup_for(size_t n) { return n >= (10 * BASE_N) ? 1 : 2; }

static agg_t run_point(const cts_vtable *vt, measure_fn fn, params_t p, size_t per_run_max) {
    int runs = runs_for(p.n), warmup = warmup_for(p.n);

    per_run_max = (per_run_max == (size_t)OP_PER_N) ? p.n : per_run_max;
    size_t buf_bytes = per_run_max * (size_t)runs * sizeof(double);

    /* Shared memory: the sample buffer plus one size_t for the real sample
     * count. The child's exit code can't carry that count (POSIX exit
     * codes are 8 bits; an unbatched op like "lifecycle" returns
     * thousands of samples per run and silently wraps mod 256). */
    void *shm = mmap(NULL, buf_bytes + sizeof(size_t), PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANON, -1, 0);
    double *buf = (double *)shm;
    size_t *total_out = (size_t *)((char *)shm + buf_bytes);
    double *tmp = malloc(per_run_max * sizeof(double));

    pid_t pid = fork();
    if (pid == 0) {
        /* CHILD PROCESS: Executes the benchmark */
        size_t total = 0;
        for (int r = 0; r < warmup + runs; r++) {
            size_t c = fn(vt, p, tmp);
            if (r >= warmup) {
                memcpy(buf + total, tmp, c * sizeof(double));
                total += c;
            }
        }
        *total_out = total;
        free(tmp);
        _exit(0); /* Exit child immediately to release all heap memory to OS */
    }

    /* PARENT PROCESS: Waits for child to finish */
    int status = 0;
    waitpid(pid, &status, 0);
    size_t total = (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? *total_out : 0;

    agg_t a = aggregate(buf, total);

    munmap(shm, buf_bytes + sizeof(size_t));
    free(tmp);
    return a;
}

static void header(FILE *f, const char *axis, int is_mem) {
    const char *u = is_mem ? "bytes" : "ns";
    fprintf(f, "%s,algo,mean_%s,std_%s,p99_%s,max_%s,n_samples,runs,warmup,seed\n",
            axis, u, u, u, u);
}

static void row(FILE *f, const char *val, const char *algo, agg_t a, size_t n) {
    fprintf(f, "%s,%s,%.4f,%.4f,%.4f,%.4f,%zu,%d,%d,%d\n",
            val, algo, a.mean, a.std, a.p99, a.max, a.n,
            runs_for(n), warmup_for(n), SEED);
}

/* ---- sweep driver ---- */

static const size_t   N_VALS[] = {1, 10, 100, 1000};
static const size_t   N_VALS_HUGE[] = {2, 5, 10, 20, 50, 80, 100, 110, 120, 150, 200};
/* Chosen by a dedicated sweep (lawn2 vs wahern, n=100K, powers of two from
 * 128 to 65536) rather than picked arbitrarily: the wheel's tick cost is
 * genuinely non-monotonic in span (a reproducible dip near 2048, a spike
 * near 4096, tied to its internal hierarchical level boundaries, not noise:
 * confirmed by rerunning the sweep independently). 1024 is the smallest
 * value past the steep small-span regime and sits on neither the dip nor
 * the spike. The span-sweep axis itself uses the same grid. */
static const uint64_t SPAN_VALS[] = {128, 1024, 4096, 8192, 65536, 524288};
static const uint64_t DISTINCT_TTL_VALS[]   = {1, 10, 100, 1000, 10000};
static const int      WORKLOAD_VALS[]   = {WL_UNIFORM, WL_BURSTY, WL_SPREAD};
static const char    *WORKLOAD_NAMES[]  = {"uniform", "bursty", "spread"};

typedef struct { const char *name; measure_fn fn; size_t per_run_max; int is_mem; } op_t;

static const op_t OPS[] = {
    {"insert",       measure_insert,   MAX_OPS / BATCH + 2, 0},
    {"delete",       measure_delete,   MAX_OPS / BATCH + 2, 0},
    {"tick_advance", measure_empty_tick,TICKS / BATCH + 2,   0},
    {"expiry",       measure_expiry,   2,                   0},
    {"memory",       measure_memory,   1,                   1},
    {"tick_scan",    measure_tick,     TICK_SCAN_SAMPLES,    0},
    {"lifecycle",    measure_lifecycle,OP_PER_N,            0},
};

static double free_and_inactive_bytes(void) {
    /* free + inactive + speculative: all three are pages macOS reclaims
     * without writing anything out (speculative is unread readahead cache),
     * so all three are "available" in the same sense. Excluding speculative
     * was undercounting real headroom, sometimes by several GB. */
    FILE *p = popen("vm_stat", "r");
    if (!p) return -1.0;
    char line[256];
    long free_pages = 0, inactive_pages = 0, speculative_pages = 0, pagesize = 16384;
    while (fgets(line, sizeof line, p)) {
        long v;
        if (sscanf(line, "page size of %ld bytes", &v) == 1) pagesize = v;
        else if (sscanf(line, "Pages free: %ld.", &v) == 1) free_pages = v;
        else if (sscanf(line, "Pages inactive: %ld.", &v) == 1) inactive_pages = v;
        else if (sscanf(line, "Pages speculative: %ld.", &v) == 1) speculative_pages = v;
    }
    pclose(p);
    return (double)(free_pages + inactive_pages + speculative_pages) * (double)pagesize;
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
    uint64_t p = 1;
    while (p < x) p <<= 1;
    return p;
}

/* Extra memory the "lifecycle" op needs beyond bytes_per_timer_estimate*n:
 * the harness's own id-indexed arrays (sized to cover the preload
 * population, its total replenishment count over the run, and the
 * foreground churn), plus, for naive specifically, its ring growing to
 * cover the preload span itself. That ring growth is not a harness
 * artifact to route around, it is naive's own documented overflow
 * weakness (Section~\ref{sec:limitations}) showing up under a genuinely
 * wide, huge-scale TTL span, exactly the scenario this test exists to
 * cover. Mirrors the regime/span/rate math in measure_lifecycle. */
static double lifecycle_extra_bytes(const char *algo, size_t n, uint64_t preload_n) {
    double preload_timer_count;
    if (preload_n) {
        preload_timer_count = (double)preload_n;
    } else {
        int huge_regime = n >= (size_t)(10 * BASE_N);
        preload_timer_count = huge_regime ? (double)BASE_N_HUGE : (double)BASE_N;
    }
    double preload_span_per_timer = (double)BASE_SPAN_HUGE * 100.0 / (double)BASE_N;
    double preload_span = preload_timer_count * preload_span_per_timer;
    if (preload_span < 1) preload_span = 1;
    double preload_avg_lifetime = preload_span / 2.0;
    double preload_replenish_rate = preload_timer_count / preload_avg_lifetime;
    double max_preload_replenish = preload_replenish_rate * (double)n + 2.0;
    double fg_id_base = preload_timer_count + max_preload_replenish;

    /* Harness arrays live[] and ttls[], each roughly (fg_id_base+n) * 8B. */
    double harness_bytes = (fg_id_base + (double)n) * 8.0 * 2.0;

    double ring_bytes = 0.0;
    if (!strcmp(algo, "naive")) {
        uint64_t ring_slots = next_pow2((uint64_t)preload_span);
        ring_bytes = (double)ring_slots * 24.0;   /* slot_t: ptr + 2 size_t */
    }
    return harness_bytes + ring_bytes;
}

static int memory_ok(const char *op, const char *algo, size_t n, uint64_t preload_n, double *need_gb_out, double *budget_gb_out) {
    double need = (bytes_per_timer_estimate(algo) + 16.0) * (double)n;
    if (!strcmp(op, "lifecycle")) {
        /* The store holds ~the background population, not the (fixed, small)
         * foreground op count n, so size the base estimate off the population
         * (preload_n, or the regime default when unset). */
        double pop = preload_n ? (double)preload_n
                   : (n >= (size_t)(10 * BASE_N) ? (double)BASE_N_HUGE : (double)BASE_N);
        need = (bytes_per_timer_estimate(algo) + 16.0) * pop;
        need += lifecycle_extra_bytes(algo, n, preload_n);
    }
    double avail = free_and_inactive_bytes();
    double budget = avail < 0 ? 1e18 : avail * (MEM_SAFETY_PCT / 100.0);
    *need_gb_out = need / 1e9;
    *budget_gb_out = budget / 1e9;
    return need <= budget;
}

static void human_readable_n(size_t n, char* n_str) {
    if (n >= (1000 * 1000)) { snprintf(n_str, sizeof n_str, "%zuM", (n / (1000 * 1000))); }
    else if (n > (1000)) {    snprintf(n_str, sizeof n_str, "%zuK", (n / (1000))); }
    else {                    snprintf(n_str, sizeof n_str, "%zu", n); }
}

static void sweep_axis(const op_t *op, const char *axis, const char *dir, bool huge) {
    char path[512];
    if (!huge) snprintf(path, sizeof path, "%s/%s_%s.csv", dir, op->name, axis);
    else snprintf(path, sizeof path, "%s/%s_%s_huge.csv", dir, op->name, axis);
    FILE *f = fopen(path, "w");
    header(f, axis, op->is_mem);

    /* memory-vs-n also emits derived bytes-per-timer */
    FILE *pf = NULL;
    if (op->is_mem && strcmp(axis, "n") == 0) {
        char pp[512];
        if (!huge) snprintf(pp, sizeof pp, "%s/memory_per_timer_n.csv", dir);
        else snprintf(pp, sizeof pp, "%s/memory_per_timer_n_huge.csv", dir);
        pf = fopen(pp, "w");
        header(pf, "n", 1);
    }

    params_t base_params = {BASE_N, BASE_SPAN, 100, WL_UNIFORM, 0};
    if (huge) {
        base_params.n = BASE_N_HUGE;
        base_params.ttl_span= BASE_SPAN_HUGE;
    }

    size_t nvals;
    if      (!strcmp(axis, "n"))            nvals = (huge) ? GET_SIZE(N_VALS_HUGE) : GET_SIZE(N_VALS);
    else if (!strcmp(axis, "ttl_span"))     nvals = GET_SIZE(SPAN_VALS);
    else if (!strcmp(axis, "distinct_ttls"))nvals = GET_SIZE(DISTINCT_TTL_VALS);
    else if (!strcmp(axis, "workload"))     nvals = GET_SIZE(WORKLOAD_VALS);
    else {
        printf("no values defined for axis \"%s\"\n", axis);
        if (pf) fclose(pf);
	fclose(f);
        return;
    }

    for (size_t vi = 0; vi < nvals; vi++) {
        params_t p = base_params;
        char valstr[32];
        if (!strcmp(axis, "n"))
        {
            size_t pop = huge ? (size_t)(1000 * 1000 * N_VALS_HUGE[vi])
                              : (size_t)(1000 * N_VALS[vi]);
            if (!strcmp(op->name, "lifecycle")) {
                /* Lifecycle sweeps the background POPULATION (preload_n); the
                 * timed foreground op count stays fixed at FG_OPS, so the
                 * n-axis genuinely means "number of background timers" instead
                 * of the foreground op count with a regime-switched population.
                 * Use one fixed span across both the non-huge and huge halves
                 * so the extended population curve has no density seam at the
                 * baseline boundary. */
                p.preload_n = pop;
                p.n = FG_OPS;
                p.ttl_span = BASE_SPAN_HUGE;
            } else {
                p.n = pop;
            }
            snprintf(valstr, 32, "%zu", pop);
        }
        else if (!strcmp(axis, "ttl_span")){ 
            p.ttl_span = SPAN_VALS[vi]; 
            snprintf(valstr, 32, "%llu", (unsigned long long)p.ttl_span); 
        }
        else if (!strcmp(axis, "distinct_ttls")){ 
            p.distinct = DISTINCT_TTL_VALS[vi]; 
            snprintf(valstr, 32, "%llu", (unsigned long long)p.distinct); 
        }
        else if (!strcmp(axis, "workload")){ 
            p.workload = WORKLOAD_VALS[vi];
            snprintf(valstr, 32, "%s", WORKLOAD_NAMES[vi]);
        }
        char n_str[32];
        human_readable_n(p.n, n_str);
        for (int a = 0; a < cts_nalgos; a++) {
            double need_gb, budget_gb;
            if (memory_ok(op->name, cts_algos[a]->name, p.n, p.preload_n, &need_gb, &budget_gb)) {
                agg_t agg = run_point(cts_algos[a], op->fn, p, op->per_run_max);
                row(f, valstr, cts_algos[a]->name, agg, p.n);
                if (pf) {
                    agg_t per = agg;
                    per.mean /= (double)p.n; per.std /= (double)p.n;
                    per.p99 /= (double)p.n; per.max /= (double)p.n;
                    row(pf, valstr, cts_algos[a]->name, per, p.n);
                }
                printf("DONE %-8s %-13s axis=%-13s value=%s n=%s\n", op->name, cts_algos[a]->name, axis, valstr, n_str);
            }
            else {
                printf("SKIP %-8s %-13s axis=%-13s value=%s n=%s (needs ~%.1fGB, safe budget ~%.1fGB at %.0f%%)\n",
                    op->name, cts_algos[a]->name, axis, valstr, n_str, need_gb, budget_gb, MEM_SAFETY_PCT);
            }
    		fflush(f);
        }
        if (pf) fflush(pf);
        fflush(f);
    }
    fclose(f);
    if (pf) fclose(pf);
    printf("  wrote %s\n", path);
}

static void run_sweeps(const char *dir, bool huge) {
    /* per_run_max = max batches per run (MAX_OPS/BATCH etc, rounded up) */
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

/* ---- inflection: tick_scan crossover in t/N ---- */

/* Two distinct, both-legitimate regimes, selected by scale_span:
 *  - scaled (true): ttl_span scales with n at the same span-per-timer ratio
 *    measure_lifecycle uses for its preload population
 *    (BASE_SPAN_HUGE*100/BASE_N), holding timer density per tick-slot
 *    constant across n. Isolates the O(t) bucket-scan crossover cleanly.
 *  - fixed (false): ttl_span stays at 10000 regardless of n, so density
 *    (n/ttl_span) grows unbounded as n grows. At large n this is dense
 *    enough to trigger wahern's own cascade pathology, its own legitimate
 *    finding (an overflow-under-fixed-span effect), but it masks the O(t)
 *    crossover, so it's reported as a separate sweep, not conflated with it. */
static double mean_tick_scan(const cts_vtable *vt, size_t n, uint64_t t, bool scale_span) {
    uint64_t ttl_span;
    if (scale_span) {
        double span_per_timer = (double)BASE_SPAN_HUGE * 100.0 / (double)BASE_N;
        ttl_span = (uint64_t)((double)n * span_per_timer);
        if (ttl_span < 1) ttl_span = 1;
    } else {
        ttl_span = 10000;
    }
    params_t p = {n, ttl_span, t, WL_UNIFORM, 0};
    int runs = n >= (10 * BASE_N) ? 3 : 5, warmup = 1;
    double *buf = malloc(TICK_SCAN_SAMPLES * sizeof *buf);
    double sum = 0; size_t count = 0;
    for (int r = 0; r < warmup + runs; r++) {
        size_t c = measure_tick(vt, p, buf);
        if (r >= warmup) { for (size_t i = 0; i < c; i++) { sum += buf[i]; count++; } }
    }
    free(buf);
    return count ? sum / (double)count : 0.0;
}

static void run_inflection(const char *dir, bool scale_span) {
    static const size_t   NS[] = {1000, 10000, 100000, 1000000, 10000000};
    static const uint64_t TS[] = {1,2,5,10,20,50,100,200,500,1000,2000,5000,10000};
    char path[512];
    snprintf(path, sizeof path, "%s/inflection%s.csv", dir, scale_span ? "" : "_fixed_span");
    FILE *f = fopen(path, "w");
    /* Per-tick (tick_scan) crossover columns, plus the full-lifecycle crossover
     * columns (lawn2/wahern only): the lifecycle op is the mixed insert/delete/
     * tick workload measured at population N (via preload_n) and distinct-TTL
     * count t. Lifecycle is measured only in the scale_span pass to avoid
     * running the heavy grid twice; the fixed_span pass writes 0 for them. */
    fprintf(f, "N,t,t_over_N,lawn_pertick_ns,lawn2_pertick_ns,wahern_pertick_ns,naive_pertick_ns,"
               "ratio_lawn_over_wahern,ratio_lawn2_over_wahern,"
               "lawn2_life_ns,wahern_life_ns,ratio_wahern_over_lawn2_life,"
               "lawn2_life_p99,wahern_life_p99,ratio_wahern_over_lawn2_life_p99,"
               "lawn2_life_max,wahern_life_max\n");

    const cts_vtable *lawn = NULL, *lawn2 = NULL, *wahern = NULL, *naive = NULL;
    for (int a = 0; a < cts_nalgos; a++) {
        if (!strcmp(cts_algos[a]->name, "lawn")) lawn = cts_algos[a];
        if (!strcmp(cts_algos[a]->name, "lawn2")) lawn2 = cts_algos[a];
        if (!strcmp(cts_algos[a]->name, "wahern")) wahern = cts_algos[a];
        if (!strcmp(cts_algos[a]->name, "naive")) naive = cts_algos[a];
    }

    printf("inflection (tick_scan + lifecycle vs wahern; crossover reported for lawn2):\n");
    for (size_t ni = 0; ni < GET_SIZE(NS); ni++) {
        size_t n = NS[ni];
        /* Lifecycle memory need depends on (FG_OPS, n) only, not t, so check
         * once per N. If it fails, lifecycle columns stay 0 for this N. */
        bool life_ok = false;
        if (scale_span) {
            double need_gb, budget_gb;
            life_ok = memory_ok("lifecycle", lawn2->name, FG_OPS, n, &need_gb, &budget_gb) &&
                      memory_ok("lifecycle", wahern->name, FG_OPS, n, &need_gb, &budget_gb);
            if (!life_ok)
                printf("  lifecycle SKIP N=%zu (needs ~%.1fGB, safe budget ~%.1fGB)\n",
                       n, need_gb, budget_gb);
        }
        double prev_ratio2 = 0, prev_t = 0, xover2 = 0;
        double prev_ratio_life = 0, xover_life = 0;
        for (size_t ti = 0; ti < sizeof TS / sizeof TS[0]; ti++) {
            uint64_t t = TS[ti];
            if (t > n || t > 10000) continue;
            double ll  = mean_tick_scan(lawn, n, t, scale_span);
            double l2l = mean_tick_scan(lawn2, n, t, scale_span);
            double wl  = mean_tick_scan(wahern, n, t, scale_span);
            double nl  = naive ? mean_tick_scan(naive, n, t, scale_span) : 0;
            double ratio  = ll / wl;
            double ratio2 = l2l / wl;
            /* Full-lifecycle cost at population n (preload_n) and distinct t.
             * Record mean, p99, and max: the wheel's mean is dominated by rare
             * O(N/t) cascade stalls (heavy-tailed, and window-sensitive since
             * the stall period far exceeds the sampling window), so p99 is the
             * robust typical-cost metric the crossover figure uses, while max
             * documents the tail-stall magnitude Lawn2 never pays. */
            double l2_life = 0, wh_life = 0, ratio_life = 0;
            double l2_p99 = 0, wh_p99 = 0, ratio_p99 = 0;
            double l2_max = 0, wh_max = 0;
            if (life_ok) {
                params_t lp = {FG_OPS, BASE_SPAN_HUGE, t, WL_UNIFORM, n};
                agg_t a2 = run_point(lawn2,  measure_lifecycle, lp, OP_PER_N);
                agg_t aw = run_point(wahern, measure_lifecycle, lp, OP_PER_N);
                l2_life = a2.mean; wh_life = aw.mean;
                l2_p99  = a2.p99;  wh_p99  = aw.p99;
                l2_max  = a2.max;  wh_max  = aw.max;
                ratio_life = l2_life > 0 ? wh_life / l2_life : 0;
                ratio_p99  = l2_p99  > 0 ? wh_p99  / l2_p99  : 0;
            }
            fprintf(f, "%zu,%llu,%.6g,%.2f,%.2f,%.2f,%.2f,%.4f,%.4f,"
                       "%.2f,%.2f,%.4f,%.2f,%.2f,%.4f,%.2f,%.2f\n",
                    n, (unsigned long long)t, (double)t / n, ll, l2l, wl, nl,
                    ratio, ratio2, l2_life, wh_life, ratio_life,
                    l2_p99, wh_p99, ratio_p99, l2_max, wh_max);
            if (ratio_life > 0 && prev_ratio_life > 0 && !xover_life &&
                ((prev_ratio_life - 1.0) * (ratio_life - 1.0) < 0)) {
                double lt0 = log((double)prev_t), lt1 = log((double)t);
                double lr0 = log(prev_ratio_life), lr1 = log(ratio_life);
                double frac = (0.0 - lr0) / (lr1 - lr0);
                xover_life = exp(lt0 + frac * (lt1 - lt0));
            }
            if (ratio_life > 0) prev_ratio_life = ratio_life;
            if (prev_t > 0 && !xover2 &&
                ((prev_ratio2 - 1.0) * (ratio2 - 1.0) < 0)) {
                double lt0 = log((double)prev_t), lt1 = log((double)t);
                double lr0 = log(prev_ratio2), lr1 = log(ratio2);
                double frac = (0.0 - lr0) / (lr1 - lr0);
                xover2 = exp(lt0 + frac * (lt1 - lt0));
            }
            printf("    distinct TTLs=%llu\n", (unsigned long long)t);
            prev_ratio2 = ratio2; prev_t = (double)t;
            fflush(f);
        }
        if (xover2)
            printf("  N=%zu: lawn2 tick_scan crossover t*=%.0f (t/N=%.2e)\n", n, xover2, xover2 / n);
        else
            printf("  N=%zu: lawn2 tick_scan %s wahern across the whole range\n",
                   n, prev_ratio2 < 1 ? "beats" : "loses to");
        if (scale_span && life_ok) {
            if (xover_life)
                printf("  N=%zu: lawn2 lifecycle crossover t*=%.0f (t/N=%.2e)\n",
                       n, xover_life, xover_life / n);
            else
                printf("  N=%zu: lawn2 lifecycle %s wahern across the whole range\n",
                       n, prev_ratio_life > 1 ? "beats" : "loses to");
        }
    }
    fclose(f);
    printf("  wrote %s\n", path);
}


/* ---- distribution: affects of ttl distribution ---- */

static void measure_distribution(const char* name, size_t ttl_count, uint64_t ttl_span, uint64_t distinct, FILE* f) {
    params_t dist_param = {ttl_count, ttl_span, distinct, WL_UNIFORM, 0};
    printf("--- %s ---\n", name);
    for (int a = 0; a < cts_nalgos; a++) {
        agg_t insert_result = run_point(cts_algos[a], measure_insert, dist_param, MAX_OPS / BATCH + 2);
        agg_t expiry_result = run_point(cts_algos[a], measure_expiry, dist_param, 2);
        printf("  %-8s insert: %.2f ns/op | expiry: %.2f ns/op\n", 
            cts_algos[a]->name, insert_result.mean, expiry_result.mean);
        fprintf(f, "%s,%s,%zu,%llu,%llu,%.2f ns/op,%.2f ns/op\n", 
            name, cts_algos[a]->name, ttl_count, ttl_span, distinct, insert_result.mean, expiry_result.mean);
    }
}


static void run_distribution(const char *dir) {
    printf("Benchmarking across distinct TTL distribuitions: Fixed t=1, Discrete t=10, Continuous t=N:\n");
    char path[512]; snprintf(path, sizeof path, "%s/ttl_distribution.csv", dir);
    FILE *f = fopen(path, "w");
    fprintf(f, "Distribution,algo,N,ttl span, distinct ttls, insert,expiry\n");

    const size_t ttl_count = 100000;
    
    measure_distribution("fixed (t=1)", ttl_count, 100, 1, f);
    measure_distribution("discrete (t=10)", ttl_count, 100, 10, f);
    measure_distribution("continuous (t=N)", ttl_count, ttl_count, ttl_count, f);
    
    fclose(f);
    printf("wrote %s\n", path);
}


static int wl_from_name(const char *s) {
    if (!strcmp(s, "uniform")) return WL_UNIFORM;
    if (!strcmp(s, "bursty"))  return WL_BURSTY;
    if (!strcmp(s, "spread"))  return WL_SPREAD;
    return -1;
}

static const cts_vtable* algo_from_name(const char *s) {
    for (int i = 0; i < cts_nalgos; i++)
        if (!strcmp(cts_algos[i]->name, s)) 
            return cts_algos[i];
    return NULL;
}

static const op_t* op_from_name(const char *s) {
    const int num_ops = GET_SIZE(OPS);
    for (int i = 0; i < num_ops; i++)
        if (!strcmp(OPS[i].name, s)) 
            return &OPS[i];
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
    if (workload < 0) {
        fprintf(stderr, "unknown workload %s\n", argv[8]);
        return 2;
    }
    double safety_pct = argc >= 10 ? strtod(argv[9], NULL) : MEM_SAFETY_PCT;
    /* Only measure_lifecycle reads this: overrides preload_timer_count
     * directly instead of the fixed BASE_N/BASE_N_HUGE regime switch. */
    uint64_t preload_n = argc == 11 ? strtoull(argv[10], NULL, 10) : 0;

    const cts_vtable *algo = algo_from_name(algoname);
    if (!algo) { 
        fprintf(stderr, "unknown algo %s\n", algoname); 
        return 2; 
    }

    const op_t *op = op_from_name(opname);
    if (!op) { 
        fprintf(stderr, "unknown op %s\n", opname); 
        return 2; 
    }

    params_t params = {n, ttl_span, distinct, workload, preload_n};

    char n_str[32];
    human_readable_n(n, n_str);
    double need_gb, budget_gb;
    if (memory_ok(op->name, algo->name, n, preload_n, &need_gb, &budget_gb)) {
        uint64_t t0 = cts_now_ns();
        agg_t agg = run_point(algo, op->fn, params, op->per_run_max);
        double secs = (double)(cts_now_ns() - t0) / 1e9;
        printf("DONE algo %s\noperation %s\nn %s\nttl_span %llu\ndistinct %llu\nworkload %s\nmean %.4f\nstd %.4f\np99 %.4f\nmax %.4f\ntotal runtime %.1fs\nruns %d\nwarmup %d\nseed %d\n",
            algo->name, op->name, n_str, ttl_span, distinct, argv[8], agg.mean, agg.std, agg.p99, agg.max, secs, runs_for(n), warmup_for(n), SEED);
    }
    else {
        printf("SKIP %-8s %-13s axis=%-13s n=%s (needs ~%.1fGB, safe budget ~%.1fGB at %.0f%%)\n",
            op->name, algo->name, axis, n_str, need_gb, budget_gb, safety_pct);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *dir = "results";
    char cmd[256]; snprintf(cmd, sizeof cmd, "mkdir -p %s", dir); if (system(cmd)) {}
    if (argc > 1) // single scenario options
    { 
        if (!strcmp(argv[1], "sweeps")) { run_sweeps(dir, false); }
        else if (!strcmp(argv[1], "dist")) { run_distribution(dir); }
        else if (!strcmp(argv[1], "inflection")) {
            if (argc >= 3) {
                run_inflection(dir, strcmp(argv[2], "fixed-span"));
            } else {
                run_inflection(dir, true);
                run_inflection(dir, false);
            }
        }
        else if (!strcmp(argv[1], "huge")) { run_sweeps(dir, true); }
        else if (!strcmp(argv[1], "single")) { return run_single(argc,  argv); }
        else if (!strcmp(argv[1], "sweep-op")) {
            if (argc < 4) {
                fprintf(stderr, "usage: %s sweep-op <op> <axis> [huge]\n", argv[0]);
                return 2;
            }
            const op_t *op = op_from_name(argv[2]);
            if (!op) { fprintf(stderr, "unknown op %s\n", argv[2]); return 2; }
            bool huge = (argc >= 5 && !strcmp(argv[4], "huge"));
            sweep_axis(op, argv[3], dir, huge);
        }
        else if (!strcmp(argv[1], "all")) {
            run_sweeps(dir, false);
            run_sweeps(dir, true);
            run_distribution(dir);
            run_inflection(dir, true);
            run_inflection(dir, false);

        }
        else { 
            printf("unrecognized option %s\n", argv[1]);
            return 1;
        }
    }
    else { // just run a quick benchmark to see everything ok
        run_sweeps(dir, false);
        run_distribution(dir);
    }
    printf("done.\n");
    return 0;
}
