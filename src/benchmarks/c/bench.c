/* C benchmark harness: mirrors src/benchmarks/python. Logical clock, one
 * function per operation, sweeps 4 axes + inflection, emits CSVs matching the
 * Python schema so the same plotting renders them. */
#include "cts.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc/malloc.h>   /* macOS: malloc_zone_statistics */

#define MAX_OPS 20000
#define TICKS   2000
#define WINDOW  200
#define SEED0   1234
#define BATCH   256   /* micro-batch: time B ops per cts_now_ns pair, one
                         (batch_time/B) sample, to clear the ~41ns clock floor */

typedef struct {
    size_t   n;
    uint64_t ttl_span;
    uint64_t distinct;
    int      workload;
    uint64_t seed;
} params_t;

typedef size_t (*measure_fn)(const cts_vtable *, params_t, double *);

/* ---- measurements (one per operation) ---- */

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
    for (size_t i = 0; i < p.n; i++) vt->start(s, i, ttls[i] + TICKS); /* nothing due in window */
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
    gen_ttls(ttls, p.n, WINDOW, p.distinct, p.workload, p.seed); /* all due within WINDOW */
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

static size_t m_memory(const cts_vtable *vt, params_t p, double *out) {
    uint64_t *ttls = malloc(p.n * sizeof *ttls);           /* before snapshot: not counted */
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

static size_t m_lifecycle(const cts_vtable *vt, params_t p, double *out) {
    uint64_t *ttls = malloc(p.n * sizeof *ttls);
    gen_ttls(ttls, p.n, p.ttl_span, p.distinct, p.workload, p.seed);
    cts_store *s = vt->create();
    uint64_t t0 = cts_now_ns();
    for (size_t i = 0; i < p.n; i++) vt->start(s, i, ttls[i]);
    while (vt->size(s)) vt->tick(s);
    double per = (double)(cts_now_ns() - t0) / (double)p.n;
    vt->destroy(s); free(ttls);
    out[0] = per;
    return 1;
}

/* ---- aggregation over runs (warmup discarded) ---- */

static agg_t run_point(const cts_vtable *vt, measure_fn fn, params_t p,
                       size_t per_run_max) {
    int heavy = (p.n >= 1000000);
    int runs = heavy ? 3 : 7, warmup = heavy ? 1 : 2;
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

static int runs_for(size_t n) { return n >= 1000000 ? 3 : 7; }
static int warmup_for(size_t n) { return n >= 1000000 ? 1 : 2; }

static void header(FILE *f, const char *axis, int is_mem) {
    const char *u = is_mem ? "bytes" : "ns";
    fprintf(f, "%s,algo,mean_%s,std_%s,p99_%s,max_%s,n_samples,runs,warmup,seed\n",
            axis, u, u, u, u);
}

static void row(FILE *f, const char *val, const char *algo, agg_t a, size_t n) {
    fprintf(f, "%s,%s,%.4f,%.4f,%.4f,%.4f,%zu,%d,%d,%d\n",
            val, algo, a.mean, a.std, a.p99, a.max, a.n,
            runs_for(n), warmup_for(n), SEED0);
}

/* ---- sweep driver ---- */

static const size_t   N_VALS[]    = {1000, 10000, 100000, 1000000};
static const uint64_t SPAN_VALS[] = {256, 2560, 25600, 256000};
static const uint64_t DT_VALS[]   = {1, 10, 100, 1000, 10000};
static const int      WL_VALS[]   = {WL_UNIFORM, WL_BURSTY, WL_SPREAD};
static const char    *WL_NAMES[]  = {"uniform", "bursty", "spread"};

static const params_t BASE = {100000, 2560, 100, WL_UNIFORM, SEED0};

typedef struct { const char *name; measure_fn fn; size_t per_run_max; int is_mem; } op_t;

static void sweep_axis(const op_t *op, const char *axis, const char *dir) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s_%s.csv", dir, op->name, axis);
    FILE *f = fopen(path, "w");
    header(f, axis, op->is_mem);

    /* memory-vs-n also emits derived bytes-per-timer */
    FILE *pf = NULL;
    if (op->is_mem && strcmp(axis, "n") == 0) {
        char pp[512]; snprintf(pp, sizeof pp, "%s/memory_per_timer_n.csv", dir);
        pf = fopen(pp, "w"); header(pf, "n", 1);
    }

    size_t nvals; int numeric = 1;
    if      (!strcmp(axis, "n"))            nvals = 4;
    else if (!strcmp(axis, "ttl_span"))     nvals = 4;
    else if (!strcmp(axis, "distinct_ttls"))nvals = 5;
    else { nvals = 3; numeric = 0; }

    for (size_t vi = 0; vi < nvals; vi++) {
        params_t p = BASE;
        char valstr[32];
        if (!strcmp(axis, "n"))            { p.n = N_VALS[vi]; snprintf(valstr, 32, "%zu", p.n); }
        else if (!strcmp(axis, "ttl_span")){ p.ttl_span = SPAN_VALS[vi]; snprintf(valstr, 32, "%llu", (unsigned long long)p.ttl_span); }
        else if (!strcmp(axis, "distinct_ttls")){ p.distinct = DT_VALS[vi]; snprintf(valstr, 32, "%llu", (unsigned long long)p.distinct); }
        else { p.workload = WL_VALS[vi]; snprintf(valstr, 32, "%s", WL_NAMES[vi]); }
        (void)numeric;

        for (int a = 0; a < cts_nalgos; a++) {
            agg_t agg = run_point(cts_algos[a], op->fn, p, op->per_run_max);
            row(f, valstr, cts_algos[a]->name, agg, p.n);
            if (pf) {
                agg_t per = agg;
                per.mean /= (double)p.n; per.std /= (double)p.n;
                per.p99 /= (double)p.n; per.max /= (double)p.n;
                row(pf, valstr, cts_algos[a]->name, per, p.n);
            }
        }
        fflush(f);
    }
    fclose(f);
    if (pf) fclose(pf);
    printf("  wrote %s\n", path);
}

/* ---- inflection: lifecycle crossover in t/N ---- */

static double mean_lifecycle(const cts_vtable *vt, size_t n, uint64_t t) {
    params_t p = {n, 10000, t, WL_UNIFORM, SEED0};
    int runs = n >= 1000000 ? 3 : 5, warmup = 1;
    double vals[16]; int c = 0;
    for (int r = 0; r < warmup + runs; r++) {
        p.seed = 100 + r;
        double out[1]; m_lifecycle(vt, p, out);
        if (r >= warmup) vals[c++] = out[0];
    }
    double sum = 0; for (int i = 0; i < c; i++) sum += vals[i];
    return sum / c;
}

static void inflection(const char *dir) {
    static const size_t   NS[] = {10000, 100000, 1000000, 10000000};
    static const uint64_t TS[] = {1,2,5,10,20,50,100,200,500,1000,2000,5000,10000};
    char path[512]; snprintf(path, sizeof path, "%s/inflection.csv", dir);
    FILE *f = fopen(path, "w");
    fprintf(f, "N,t,t_over_N,lawn_life_ns,lawn2_life_ns,wahern_life_ns,naive_life_ns,"
               "ratio_lawn_over_wahern,ratio_lawn2_over_wahern\n");

    const cts_vtable *lawn = NULL, *lawn2 = NULL, *wahern = NULL, *naive = NULL;
    for (int a = 0; a < cts_nalgos; a++) {
        if (!strcmp(cts_algos[a]->name, "lawn")) lawn = cts_algos[a];
        if (!strcmp(cts_algos[a]->name, "lawn2")) lawn2 = cts_algos[a];
        if (!strcmp(cts_algos[a]->name, "wahern")) wahern = cts_algos[a];
        if (!strcmp(cts_algos[a]->name, "naive")) naive = cts_algos[a];
    }
    printf("inflection (lifecycle vs wahern; crossover reported for lawn2):\n");
    for (size_t ni = 0; ni < 4; ni++) {
        size_t n = NS[ni];
        double prev_ratio2 = 0, prev_t = 0, xover2 = 0;
        for (size_t ti = 0; ti < sizeof TS / sizeof TS[0]; ti++) {
            uint64_t t = TS[ti];
            if (t > n || t > 10000) continue;
            double ll  = mean_lifecycle(lawn, n, t);
            double l2l = mean_lifecycle(lawn2, n, t);
            double wl  = mean_lifecycle(wahern, n, t);
            double nl  = naive ? mean_lifecycle(naive, n, t) : 0;
            double ratio  = ll / wl;
            double ratio2 = l2l / wl;
            fprintf(f, "%zu,%llu,%.6g,%.2f,%.2f,%.2f,%.2f,%.4f,%.4f\n",
                    n, (unsigned long long)t, (double)t / n, ll, l2l, wl, nl,
                    ratio, ratio2);
            if (prev_t > 0 && !xover2 &&
                ((prev_ratio2 - 1.0) * (ratio2 - 1.0) < 0)) {
                double lt0 = log((double)prev_t), lt1 = log((double)t);
                double lr0 = log(prev_ratio2), lr1 = log(ratio2);
                double frac = (0.0 - lr0) / (lr1 - lr0);
                xover2 = exp(lt0 + frac * (lt1 - lt0));
            }
            prev_ratio2 = ratio2; prev_t = (double)t;
            fflush(f);
        }
        if (xover2)
            printf("  N=%zu: lawn2 crossover t*=%.0f (t/N=%.2e)\n", n, xover2, xover2 / n);
        else
            printf("  N=%zu: lawn2 %s wahern across the whole range\n",
                   n, prev_ratio2 < 1 ? "beats" : "loses to");
    }
    fclose(f);
    printf("  wrote %s\n", path);
}

int main(int argc, char **argv) {
    const char *dir = "results_c";
    char cmd[256]; snprintf(cmd, sizeof cmd, "mkdir -p %s", dir); if (system(cmd)) {}
    if (argc > 1 && !strcmp(argv[1], "inflection")) {
        inflection(dir); printf("done.\n"); return 0;
    }

    /* per_run_max = max batches per run (MAX_OPS/BATCH etc, rounded up) */
    op_t ops[] = {
        {"insert",       m_insert, MAX_OPS / BATCH + 2, 0},
        {"delete",       m_delete, MAX_OPS / BATCH + 2, 0},
        {"tick_advance", m_tick,   TICKS / BATCH + 2,   0},
        {"expiry",       m_expiry, 2,                   0},
        {"memory",       m_memory, 1,                   1},
    };
    const char *axes[] = {"n", "ttl_span", "distinct_ttls", "workload"};

    printf("C benchmark suite | algos:");
    for (int a = 0; a < cts_nalgos; a++) printf(" %s", cts_algos[a]->name);
    printf(" -> %s\n", dir);

    for (size_t o = 0; o < sizeof ops / sizeof ops[0]; o++)
        for (size_t x = 0; x < 4; x++) {
            printf("  %s vs %s ...\n", ops[o].name, axes[x]); fflush(stdout);
            sweep_axis(&ops[o], axes[x], dir);
        }

    inflection(dir);
    printf("done.\n");
    return 0;
}
