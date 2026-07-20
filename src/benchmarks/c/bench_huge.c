/* Huge-scale N-only sweep: N in {1,2,4,10,20,40,100} million timers, with
 * distinct TTLs FIXED at 10000 (and ttl_span=10000 so the 10000 distinct
 * values are not clamped down, see util.c gen_ttls) to save time versus a
 * full axis sweep, per an ad-hoc request. Not part of the article pipeline;
 * exploratory only. Duplicates bench.c's measurement functions (rather than
 * refactoring the tracked bench.c) to keep zero risk to the existing suite.
 *
 * Memory safety: at 100M timers a single store can run into multiple GB
 * (e.g. reference lawn's ~113 B/timer at 1M projects to >11GB at 100M on a
 * 24GB machine), so before building a store for a given (op, algo, N) this
 * estimates the requirement from a conservative bytes-per-timer table and
 * skips (logging why, no crash) rather than risk exhausting memory. Each
 * measurement function still builds/destroys exactly one store at a time
 * in-process, so at most one store's worth of memory is live at once.
 */
#include "cts.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <malloc/malloc.h>   /* macOS: malloc_zone_statistics */

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

/* ---- measurements (verbatim copies of bench.c; see that file for comments) ---- */

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
    int runs = 3, warmup = 1;   /* huge scale: keep it light regardless */
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

static void header(FILE *f, int is_mem) {
    const char *u = is_mem ? "bytes" : "ns";
    fprintf(f, "n,algo,mean_%s,std_%s,p99_%s,max_%s,n_samples,runs,warmup,seed\n", u, u, u, u);
}

static void row(FILE *f, size_t n, const char *algo, agg_t a) {
    fprintf(f, "%zu,%s,%.4f,%.4f,%.4f,%.4f,%zu,3,1,%d\n",
            n, algo, a.mean, a.std, a.p99, a.max, a.n, SEED0);
}

/* ---- memory guard ---- */

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
    /* Conservative: padded well above the ~1M-scale measurements in
     * results_c/memory_per_timer_n.csv (lawn ~113, lawn2 ~40, wahern ~88,
     * naive ~38 B/timer) to leave margin for any worse scaling at 100M. */
    if (!strcmp(algo, "lawn"))   return 170.0;
    if (!strcmp(algo, "lawn2"))  return 55.0;
    if (!strcmp(algo, "wahern")) return 110.0;
    if (!strcmp(algo, "naive"))  return 65.0;
    return 200.0;
}

/* Also account for the two full-size uint64 TTL/permutation arrays every
 * measurement allocates alongside the store (8 or 16 bytes/timer). */
static int memory_ok(const char *algo, size_t n, double *need_gb_out, double *budget_gb_out) {
    double need = (bytes_per_timer_estimate(algo) + 16.0) * (double)n;
    double avail = free_and_inactive_bytes();
    double budget = avail < 0 ? 1e18 /* unknown: don't block */ : avail * 0.55;
    *need_gb_out = need / 1e9;
    *budget_gb_out = budget / 1e9;
    return need <= budget;
}

/* ---- sweep driver: n axis only, distinct/ttl_span fixed at 10000 ---- */

static const size_t N_VALS[] = {1000000, 2000000, 4000000, 10000000, 20000000, 40000000, 100000000};
#define NNV (sizeof(N_VALS) / sizeof(N_VALS[0]))

typedef struct { const char *name; measure_fn fn; size_t per_run_max; int is_mem; } op_t;

int main(void) {
    const char *dir = "results_c/huge_scale";
    if (system("mkdir -p results_c/huge_scale")) {}

    op_t ops[] = {
        {"insert",       m_insert, MAX_OPS / BATCH + 2, 0},
        {"delete",       m_delete, MAX_OPS / BATCH + 2, 0},
        {"tick_advance", m_tick,   TICKS / BATCH + 2,   0},
        {"expiry",       m_expiry, 2,                   0},
        {"memory",       m_memory, 1,                   1},
    };

    printf("huge-scale sweep | N=1e6..1e8 | ttl_span=10000 distinct=10000 uniform | algos:");
    for (int a = 0; a < cts_nalgos; a++) printf(" %s", cts_algos[a]->name);
    printf("\n");

    for (size_t o = 0; o < sizeof ops / sizeof ops[0]; o++) {
        char path[512]; snprintf(path, sizeof path, "%s/%s_n.csv", dir, ops[o].name);
        FILE *f = fopen(path, "w");
        header(f, ops[o].is_mem);
        printf("%s vs n ...\n", ops[o].name); fflush(stdout);

        for (size_t vi = 0; vi < NNV; vi++) {
            size_t n = N_VALS[vi];
            for (int a = 0; a < cts_nalgos; a++) {
                const cts_vtable *vt = cts_algos[a];
                double need_gb, budget_gb;
                if (!memory_ok(vt->name, n, &need_gb, &budget_gb)) {
                    printf("  SKIP %s n=%zu (needs ~%.1fGB, safe budget ~%.1fGB)\n",
                           vt->name, n, need_gb, budget_gb);
                    fflush(stdout);
                    continue;
                }
                params_t p = {n, 10000, 10000, WL_UNIFORM, SEED0};
                uint64_t t0 = cts_now_ns();
                agg_t agg = run_point(vt, ops[o].fn, p, ops[o].per_run_max);
                double secs = (double)(cts_now_ns() - t0) / 1e9;
                row(f, n, vt->name, agg);
                fflush(f);
                printf("  %-8s n=%-11zu mean=%.2f  (%.1fs)\n", vt->name, n, agg.mean, secs);
                fflush(stdout);
            }
        }
        fclose(f);
        printf("  wrote %s\n", path);
    }
    printf("done.\n");
    return 0;
}
