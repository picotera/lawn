/* Single-cell runner for the full 4-axis sweep (n, ttl_span, distinct_ttls,
 * workload) at scales beyond the main suite's baseline. This is a general
 * per-process exploratory tool for large Ns, running one cell per process
 * and letting the OS fully reclaim memory between attempts.
 *
 *   ./bench_huge <op> <algo> <axis> <n> <ttl_span> <distinct> <workload> [safety_pct]
 *
 * Appends one row to results_c/huge_full_sweep/<op>_<axis>.csv (axis is just
 * a label for the filename/column header, matching bench.c's convention; it
 * does not change what is measured, only the file it's written to and the
 * value written in the first CSV column).
 *
 * safety_pct is the fraction of currently-free+inactive+speculative memory
 * (see free_and_inactive_bytes) this run is allowed to use, as a percentage
 * (e.g. 85 means 85%). Defaults to 85 if omitted: conservative enough for
 * routine sweeps, while still overridable per-invocation for a one-off run
 * that needs to approach the memory ceiling (this session pushed as high as
 * 98% for specific maximal-scale points).
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
#define DEFAULT_SAFETY_PCT 85.0

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

static void row(FILE *f, const char *val, const char *algo, agg_t a) {
    fprintf(f, "%s,%s,%.4f,%.4f,%.4f,%.4f,%zu,3,1,%d\n",
            val, algo, a.mean, a.std, a.p99, a.max, a.n, SEED0);
}

/* ---- memory guard ---- */

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

static int memory_ok(const char *algo, size_t n, double safety_pct,
                      double *need_gb_out, double *budget_gb_out) {
    double need = (bytes_per_timer_estimate(algo) + 16.0) * (double)n;
    double avail = free_and_inactive_bytes();
    double budget = avail < 0 ? 1e18 : avail * (safety_pct / 100.0);
    *need_gb_out = need / 1e9;
    *budget_gb_out = budget / 1e9;
    return need <= budget;
}

static int wl_from_name(const char *s) {
    if (!strcmp(s, "uniform")) return WL_UNIFORM;
    if (!strcmp(s, "bursty"))  return WL_BURSTY;
    if (!strcmp(s, "spread"))  return WL_SPREAD;
    return -1;
}

int main(int argc, char **argv) {
    if (argc != 8 && argc != 9) {
        fprintf(stderr, "usage: %s <op> <algo> <axis_label> <n> <ttl_span> <distinct> <workload> [safety_pct]\n", argv[0]);
        return 2;
    }
    const char *opname = argv[1], *algoname = argv[2], *axis = argv[3];
    size_t n = (size_t)strtoull(argv[4], NULL, 10);
    uint64_t ttl_span = strtoull(argv[5], NULL, 10);
    uint64_t distinct = strtoull(argv[6], NULL, 10);
    int workload = wl_from_name(argv[7]);
    if (workload < 0) { fprintf(stderr, "unknown workload %s\n", argv[7]); return 2; }
    double safety_pct = argc == 9 ? strtod(argv[8], NULL) : DEFAULT_SAFETY_PCT;

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
    if (!memory_ok(algoname, n, safety_pct, &need_gb, &budget_gb)) {
        printf("SKIP %-8s %-13s axis=%-13s n=%-11zu (needs ~%.1fGB, safe budget ~%.1fGB at %.0f%%)\n",
               opname, algoname, axis, n, need_gb, budget_gb, safety_pct);
        return 0;
    }

    params_t p = {n, ttl_span, distinct, workload, SEED0};
    uint64_t t0 = cts_now_ns();
    agg_t agg = run_point(vt, fn, p, per_run_max);
    double secs = (double)(cts_now_ns() - t0) / 1e9;

    if (system("mkdir -p results_c/huge_full_sweep")) {}
    char path[512]; snprintf(path, sizeof path, "results_c/huge_full_sweep/%s_%s.csv", opname, axis);
    FILE *f = fopen(path, "a");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    /* value column: whichever parameter this axis varies */
    char valstr[32];
    if (!strcmp(axis, "n"))                 snprintf(valstr, sizeof valstr, "%zu", n);
    else if (!strcmp(axis, "ttl_span"))     snprintf(valstr, sizeof valstr, "%llu", (unsigned long long)ttl_span);
    else if (!strcmp(axis, "distinct_ttls"))snprintf(valstr, sizeof valstr, "%llu", (unsigned long long)distinct);
    else                                    snprintf(valstr, sizeof valstr, "%s", argv[7]);
    row(f, valstr, algoname, agg);
    fclose(f);

    printf("OK   %-8s %-13s axis=%-13s n=%-11zu ttl_span=%-9llu distinct=%-7llu %-8s mean=%.2f  (%.1fs, safety=%.0f%%)\n",
           opname, algoname, axis, n, (unsigned long long)ttl_span, (unsigned long long)distinct, argv[7], agg.mean, secs, safety_pct);
    return 0;
}
