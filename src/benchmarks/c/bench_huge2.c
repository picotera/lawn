/* Fills in exactly the (op, algo, n) cells that bench_huge.c's first pass
 * skipped, and APPENDS them to the existing results_c/huge_scale CSV files
 * (no header rewrite, no re-running of the already-successful 1M/2M/4M/10M
 * rows, no duplicate rows for cells that already succeeded).
 *
 * Missing-cell list is hardcoded from the first run's log:
 *   - insert/delete/tick_advance/memory: (lawn,20M), (wahern,20M), and all
 *     four algos at 40M and 100M.
 *   - expiry: same, except wahern@20M already succeeded there, so it is
 *     NOT re-run (would create a duplicate row).
 *
 * Same memory guard as bench_huge.c, but with the padding tightened using
 * the real per-timer bytes now measured up to 10-20M (results_c/huge_scale/
 * memory_n.csv: lawn ~109.5 B/timer @10M, lawn2 ~40.0 flat to 20M, wahern
 * ~93.4 @10M, naive ~49.6 @20M after an array-doubling step). Still skips
 * (never crashes) rather than force a point through.
 */
#include "cts.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc/malloc.h>

#define MAX_OPS 20000
#define TICKS   2000
#define WINDOW  200
#define SEED0   1234
#define BATCH   256

typedef struct {
    size_t   n;
    uint64_t ttl_span;
    uint64_t distinct;
    int      workload;
    uint64_t seed;
} params_t;

typedef size_t (*measure_fn)(const cts_vtable *, params_t, double *);

/* ---- measurements (verbatim copies; see bench.c for comments) ---- */

static size_t m_insert(const cts_vtable *vt, params_t p, double *out) {
    uint64_t *ttls = malloc(p.n * sizeof *ttls);
    gen_ttls(ttls, p.n, p.ttl_span, p.distinct, p.workload, p.seed);
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

static size_t m_delete(const cts_vtable *vt, params_t p, double *out) {
    uint64_t *ttls = malloc(p.n * sizeof *ttls);
    gen_ttls(ttls, p.n, p.ttl_span, p.distinct, p.workload, p.seed);
    cts_store *s = vt->create();
    for (size_t i = 0; i < p.n; i++) vt->start(s, i, ttls[i]);
    uint64_t *perm = malloc(p.n * sizeof *perm);
    gen_shuffle(perm, p.n, p.seed ^ 0xD1E7ULL);
    size_t timed = p.n < MAX_OPS ? p.n : MAX_OPS, c = 0;
    for (size_t k = 0; k < timed; ) {
        size_t bs = (timed - k) < BATCH ? (timed - k) : BATCH;
        uint64_t t0 = cts_now_ns();
        for (size_t j = 0; j < bs; j++) vt->stop(s, perm[k + j]);
        out[c++] = (double)(cts_now_ns() - t0) / (double)bs;
        k += bs;
    }
    vt->destroy(s); free(ttls); free(perm);
    return c;
}

static size_t m_tick(const cts_vtable *vt, params_t p, double *out) {
    uint64_t *ttls = malloc(p.n * sizeof *ttls);
    gen_ttls(ttls, p.n, p.ttl_span, p.distinct, p.workload, p.seed);
    cts_store *s = vt->create();
    for (size_t i = 0; i < p.n; i++) vt->start(s, i, ttls[i] + TICKS);
    size_t c = 0;
    for (int t = 0; t < TICKS; ) {
        int bs = (TICKS - t) < BATCH ? (TICKS - t) : BATCH;
        uint64_t t0 = cts_now_ns();
        for (int j = 0; j < bs; j++) vt->tick(s);
        out[c++] = (double)(cts_now_ns() - t0) / (double)bs;
        t += bs;
    }
    vt->destroy(s); free(ttls);
    return c;
}

static size_t m_expiry(const cts_vtable *vt, params_t p, double *out) {
    uint64_t *ttls = malloc(p.n * sizeof *ttls);
    gen_ttls(ttls, p.n, WINDOW, p.distinct, p.workload, p.seed);
    cts_store *s = vt->create();
    for (size_t i = 0; i < p.n; i++) vt->start(s, i, ttls[i]);
    size_t c = 0;
    for (int t = 0; t < WINDOW; ) {
        int bs = (WINDOW - t) < BATCH ? (WINDOW - t) : BATCH;
        uint64_t t0 = cts_now_ns();
        for (int j = 0; j < bs; j++) vt->tick(s);
        out[c++] = (double)(cts_now_ns() - t0) / (double)bs;
        t += bs;
    }
    vt->destroy(s); free(ttls);
    return c;
}

static size_t m_memory(const cts_vtable *vt, params_t p, double *out) {
    uint64_t *ttls = malloc(p.n * sizeof *ttls);
    gen_ttls(ttls, p.n, p.ttl_span, p.distinct, p.workload, p.seed);
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

static agg_t run_point(const cts_vtable *vt, measure_fn fn, params_t p, size_t per_run_max) {
    int runs = 3, warmup = 1;
    double *buf = malloc(per_run_max * (size_t)runs * sizeof(double));
    double *tmp = malloc(per_run_max * sizeof(double));
    size_t total = 0;
    for (int r = 0; r < warmup + runs; r++) {
        p.seed = SEED0 + r;
        size_t c = fn(vt, p, tmp);
        if (r >= warmup) { memcpy(buf + total, tmp, c * sizeof(double)); total += c; }
    }
    agg_t a = aggregate(buf, total);
    free(buf); free(tmp);
    return a;
}

static void row(FILE *f, size_t n, const char *algo, agg_t a) {
    fprintf(f, "%zu,%s,%.4f,%.4f,%.4f,%.4f,%zu,3,1,%d\n",
            n, algo, a.mean, a.std, a.p99, a.max, a.n, SEED0);
}

/* ---- memory guard, tightened using the real 1M-20M measurements ---- */

static double free_and_inactive_bytes(void) {
    FILE *p = popen("vm_stat", "r");
    if (!p) return -1.0;
    char line[256];
    long free_pages = 0, inactive_pages = 0, pagesize = 16384;
    while (fgets(line, sizeof line, p)) {
        long v;
        if (sscanf(line, "page size of %ld bytes", &v) == 1) pagesize = v;
        else if (sscanf(line, "Pages free: %ld.", &v) == 1) free_pages = v;
        else if (sscanf(line, "Pages inactive: %ld.", &v) == 1) inactive_pages = v;
    }
    pclose(p);
    return (double)(free_pages + inactive_pages) * (double)pagesize;
}

static double bytes_per_timer_estimate(const char *algo) {
    /* ~15-20% above the largest real measurement so far (results_c/huge_scale/
     * memory_n.csv): lawn 109.5 B/timer @10M, lawn2 40.0 flat to 20M, wahern
     * 93.4 @10M, naive 49.6 @20M (post doubling-step). */
    if (!strcmp(algo, "lawn"))   return 125.0;
    if (!strcmp(algo, "lawn2"))  return 48.0;
    if (!strcmp(algo, "wahern")) return 110.0;
    if (!strcmp(algo, "naive"))  return 58.0;
    return 150.0;
}

static int memory_ok(const char *algo, size_t n, double *need_gb_out, double *budget_gb_out) {
    double need = (bytes_per_timer_estimate(algo) + 16.0) * (double)n;
    double avail = free_and_inactive_bytes();
    double budget = avail < 0 ? 1e18 : avail * 0.55;
    *need_gb_out = need / 1e9;
    *budget_gb_out = budget / 1e9;
    return need <= budget;
}

/* ---- missing-cell list (from the first run's SKIP log) ---- */

typedef struct { const char *name; measure_fn fn; size_t per_run_max; int is_mem; } op_t;

/* which algos are missing at n=20,000,000, per op (40M/100M are always all four) */
static const char *MISSING_20M_DEFAULT[] = {"lawn", "wahern", NULL};
static const char *MISSING_20M_EXPIRY[]   = {"lawn", NULL}; /* wahern@20M already succeeded */
static const char *ALL_ALGOS[]            = {"lawn", "lawn2", "wahern", "naive", NULL};

static const cts_vtable *find_algo(const char *name) {
    for (int a = 0; a < cts_nalgos; a++)
        if (!strcmp(cts_algos[a]->name, name)) return cts_algos[a];
    return NULL;
}

static void run_missing_for_op(const op_t *op, const char **missing_20m) {
    char path[512]; snprintf(path, sizeof path, "results_c/huge_scale/%s_n.csv", op->name);
    FILE *f = fopen(path, "a");
    if (!f) { printf("  ERROR: cannot open %s for append\n", path); return; }

    printf("%s: filling missing cells ...\n", op->name); fflush(stdout);

    /* n = 20,000,000: only the algos that were skipped the first time. */
    for (int i = 0; missing_20m[i]; i++) {
        const cts_vtable *vt = find_algo(missing_20m[i]);
        double need_gb, budget_gb;
        size_t n = 20000000;
        if (!memory_ok(vt->name, n, &need_gb, &budget_gb)) {
            printf("  SKIP %-8s n=%-11zu (needs ~%.1fGB, safe budget ~%.1fGB)\n",
                   vt->name, n, need_gb, budget_gb);
            continue;
        }
        params_t p = {n, 10000, 10000, WL_UNIFORM, SEED0};
        uint64_t t0 = cts_now_ns();
        agg_t agg = run_point(vt, op->fn, p, op->per_run_max);
        double secs = (double)(cts_now_ns() - t0) / 1e9;
        row(f, n, vt->name, agg);
        fflush(f);
        printf("  %-8s n=%-11zu mean=%.2f  (%.1fs)\n", vt->name, n, agg.mean, secs);
        fflush(stdout);
    }

    /* n = 40,000,000 and 100,000,000: all four algos were missing. */
    static const size_t BIG_N[] = {40000000, 100000000};
    for (int bi = 0; bi < 2; bi++) {
        size_t n = BIG_N[bi];
        for (int i = 0; ALL_ALGOS[i]; i++) {
            const cts_vtable *vt = find_algo(ALL_ALGOS[i]);
            double need_gb, budget_gb;
            if (!memory_ok(vt->name, n, &need_gb, &budget_gb)) {
                printf("  SKIP %-8s n=%-11zu (needs ~%.1fGB, safe budget ~%.1fGB)\n",
                       vt->name, n, need_gb, budget_gb);
                continue;
            }
            params_t p = {n, 10000, 10000, WL_UNIFORM, SEED0};
            uint64_t t0 = cts_now_ns();
            agg_t agg = run_point(vt, op->fn, p, op->per_run_max);
            double secs = (double)(cts_now_ns() - t0) / 1e9;
            row(f, n, vt->name, agg);
            fflush(f);
            printf("  %-8s n=%-11zu mean=%.2f  (%.1fs)\n", vt->name, n, agg.mean, secs);
            fflush(stdout);
        }
    }

    fclose(f);
    printf("  appended to %s\n", path);
}

int main(void) {
    op_t ops[] = {
        {"insert",       m_insert, MAX_OPS / BATCH + 2, 0},
        {"delete",       m_delete, MAX_OPS / BATCH + 2, 0},
        {"tick_advance", m_tick,   TICKS / BATCH + 2,   0},
        {"expiry",       m_expiry, 2,                   0},
        {"memory",       m_memory, 1,                   1},
    };

    printf("huge-scale rerun (missing cells only) | ttl_span=10000 distinct=10000 uniform\n");

    run_missing_for_op(&ops[0], MISSING_20M_DEFAULT); /* insert */
    run_missing_for_op(&ops[1], MISSING_20M_DEFAULT); /* delete */
    run_missing_for_op(&ops[2], MISSING_20M_DEFAULT); /* tick_advance */
    run_missing_for_op(&ops[3], MISSING_20M_EXPIRY);  /* expiry (wahern@20M already have it) */
    run_missing_for_op(&ops[4], MISSING_20M_DEFAULT); /* memory */

    printf("done.\n");
    return 0;
}
