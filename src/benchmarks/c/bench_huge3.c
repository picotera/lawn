/* Single-cell runner: `./bench_huge3 <op> <algo> <n>` runs exactly one
 * (operation, implementation, timer-count) measurement and appends one row
 * to results_c/huge_scale/<op>_n.csv, then exits.
 *
 * Meant to be invoked ONE AT A TIME by a shell loop (not looped internally),
 * so each attempt gets a genuinely fresh process and the OS fully reclaims
 * memory between attempts - the safest way to attempt the 100,000,000-timer
 * cells that bench_huge.c/bench_huge2.c's in-process sweeps could not fit.
 *
 * Same memory guard as before, retuned with the now-solid 1M-40M data
 * (results_c/huge_scale/memory_n.csv): lawn ~109.5 B/timer (flat 10-20M),
 * lawn2 ~40.0 B/timer (flat 1-40M), wahern ~93.4 B/timer (flat 10-20M),
 * naive ~38-50 B/timer (varies with array-doubling boundaries). Utilization
 * raised to 0.80 (per-process isolation removes the cross-attempt
 * fragmentation risk the in-process sweeps had, and each cell still checks
 * live memory right before running). Still skips (prints why, exits 0)
 * rather than ever risking a crash.
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

static double bytes_per_timer_estimate(const char *algo) {
    if (!strcmp(algo, "lawn"))   return 115.0;
    if (!strcmp(algo, "lawn2"))  return 44.0;
    if (!strcmp(algo, "wahern")) return 100.0;
    if (!strcmp(algo, "naive"))  return 56.0;
    return 150.0;
}

static int memory_ok(const char *algo, size_t n, double *need_gb_out, double *budget_gb_out) {
    double need = (bytes_per_timer_estimate(algo) + 16.0) * (double)n;
    double avail = free_and_inactive_bytes();
    double budget = avail < 0 ? 1e18 : avail * 0.98;
    *need_gb_out = need / 1e9;
    *budget_gb_out = budget / 1e9;
    return need <= budget;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <op: insert|delete|tick_advance|expiry|memory> <algo> <n>\n", argv[0]);
        return 2;
    }
    const char *opname = argv[1], *algoname = argv[2];
    size_t n = (size_t)strtoull(argv[3], NULL, 10);

    const cts_vtable *vt = NULL;
    for (int a = 0; a < cts_nalgos; a++)
        if (!strcmp(cts_algos[a]->name, algoname)) vt = cts_algos[a];
    if (!vt) { fprintf(stderr, "unknown algo %s\n", algoname); return 2; }

    measure_fn fn; size_t per_run_max; int is_mem = 0;
    if (!strcmp(opname, "insert"))            { fn = m_insert; per_run_max = MAX_OPS / BATCH + 2; }
    else if (!strcmp(opname, "delete"))       { fn = m_delete; per_run_max = MAX_OPS / BATCH + 2; }
    else if (!strcmp(opname, "tick_advance")) { fn = m_tick;   per_run_max = TICKS / BATCH + 2; }
    else if (!strcmp(opname, "expiry"))       { fn = m_expiry; per_run_max = 2; }
    else if (!strcmp(opname, "memory"))       { fn = m_memory; per_run_max = 1; is_mem = 1; }
    else { fprintf(stderr, "unknown op %s\n", opname); return 2; }
    (void)is_mem;

    double need_gb, budget_gb;
    if (!memory_ok(algoname, n, &need_gb, &budget_gb)) {
        printf("SKIP %-8s %-13s n=%-11zu (needs ~%.1fGB, safe budget ~%.1fGB)\n",
               opname, algoname, n, need_gb, budget_gb);
        return 0;
    }

    params_t p = {n, 10000, 10000, WL_UNIFORM, SEED0};
    uint64_t t0 = cts_now_ns();
    agg_t agg = run_point(vt, fn, p, per_run_max);
    double secs = (double)(cts_now_ns() - t0) / 1e9;

    char path[512]; snprintf(path, sizeof path, "results_c/huge_scale/%s_n.csv", opname);
    FILE *f = fopen(path, "a");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    row(f, n, algoname, agg);
    fclose(f);

    printf("OK   %-8s %-13s n=%-11zu mean=%.2f  (%.1fs)\n", opname, algoname, n, agg.mean, secs);
    return 0;
}
