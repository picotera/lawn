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
#define TICKS   2000
#define WINDOW  200
#define BATCH   256   /* micro-batch: time B ops per cts_now_ns pair, one
                         (batch_time/B) sample, to clear the ~41ns clock floor */


#define BASE_N (100 * 1000)
#define BASE_SPAN 1024

#define BASE_N_HUGE (10 * 1000 * 1000)
#define BASE_SPAN_HUGE 4096

typedef struct {
    size_t   n;
    uint64_t ttl_span;
    uint64_t distinct;
    int      workload;
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

static size_t measure_tick(const cts_vtable *vt, params_t p, double *out) {
    uint64_t *ttls = malloc(p.n * sizeof *ttls);
    gen_ttls(ttls, p.n, p.ttl_span, p.distinct, p.workload, SEED);
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

static size_t measure_lifecycle(const cts_vtable *vt, params_t p, double *out) {
    uint64_t *ttls = malloc(p.n * sizeof *ttls);
    gen_ttls(ttls, p.n, p.ttl_span, p.distinct, p.workload, SEED);
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

static int runs_for(size_t n) { return n >= (10 * BASE_N) ? 3 : 7; }
static int warmup_for(size_t n) { return n >= (10 * BASE_N) ? 1 : 2; }

static agg_t run_point(const cts_vtable *vt, measure_fn fn, params_t p, size_t per_run_max) {
    int runs = runs_for(p.n), warmup = warmup_for(p.n);
    size_t buf_bytes = per_run_max * (size_t)runs * sizeof(double);
    
    /* Allocate shared memory so the child process can return data to parent */
    double *buf = mmap(NULL, buf_bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
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
        free(tmp);
        _exit((int)total); /* Exit child immediately to release all heap memory to OS */
    }

    /* PARENT PROCESS: Waits for child to finish */
    int status = 0;
    waitpid(pid, &status, 0);
    size_t total = WIFEXITED(status) ? WEXITSTATUS(status) : 0;

    agg_t a = aggregate(buf, total);
    
    munmap(buf, buf_bytes);
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
static const uint64_t SPAN_VALS[] = {128, 1024, 4096, 8192, 65536};
static const uint64_t DISTINCT_TTL_VALS[]   = {1, 10, 100, 1000, 10000};
static const int      WORKLOAD_VALS[]   = {WL_UNIFORM, WL_BURSTY, WL_SPREAD};
static const char    *WORKLOAD_NAMES[]  = {"uniform", "bursty", "spread"};

typedef struct { const char *name; measure_fn fn; size_t per_run_max; int is_mem; } op_t;

static const op_t OPS[] = {
    {"insert",       measure_insert,   MAX_OPS / BATCH + 2, 0},
    {"delete",       measure_delete,   MAX_OPS / BATCH + 2, 0},
    {"tick_advance", measure_tick,     TICKS / BATCH + 2,   0},
    {"expiry",       measure_expiry,   2,                   0},
    {"memory",       measure_memory,   1,                   1},
    {"lifecycle",    measure_lifecycle,1,                   0},
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

    params_t base_params = BASE_PARAMS;
    if (huge){
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
            if (huge) {
                p.n = 1000 * 1000 * N_VALS_HUGE[vi];
            }
            else {
                p.n = 1000 * N_VALS[vi];
            }
            
            snprintf(valstr, 32, "%zu", p.n);
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
            if (memory_ok(cts_algos[a]->name, p.n, &need_gb, &budget_gb)) {
                agg_t agg = run_point(cts_algos[a], op->fn, p, op->per_run_max);
                row(f, valstr, cts_algos[a]->name, agg, p.n);
                if (pf) {
                    agg_t per = agg;
                    per.mean /= (double)p.n; per.std /= (double)p.n;
                    per.p99 /= (double)p.n; per.max /= (double)p.n;
                    row(pf, valstr, cts_algos[a]->name, per, p.n);
                }
                printf("DONE %-8s %-13s axis=%-13s n=%s\n", op->name, cts_algos[a]->name, axis, n_str);
            }
            else {
                printf("SKIP %-8s %-13s axis=%-13s n=%s (needs ~%.1fGB, safe budget ~%.1fGB at %.0f%%)\n",
                    op->name, cts_algos[a]->name, axis, n_str, need_gb, budget_gb, MEM_SAFETY_PCT);
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

/* ---- inflection: lifecycle crossover in t/N ---- */

static double mean_lifecycle(const cts_vtable *vt, size_t n, uint64_t t) {
    params_t p = {n, 10000, t, WL_UNIFORM};
    int runs = n >= (10 * BASE_N) ? 3 : 5, warmup = 1;
    double vals[16]; int c = 0;
    for (int r = 0; r < warmup + runs; r++) {
        double out[1]; 
        measure_lifecycle(vt, p, out);
        if (r >= warmup) vals[c++] = out[0];
    }
    double sum = 0; for (int i = 0; i < c; i++) sum += vals[i];
    return sum / c;
}

static void run_inflection(const char *dir) {
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

static void measure_distribution(const char* name, size_t ttl_count, uint64_t ttl_span, uint64_t distinct, FILE* f) {
    params_t dist_param = {ttl_count, ttl_span, distinct, WL_UNIFORM};
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
    if (argc != 9 && argc != 10) {
        fprintf(stderr, "usage: %s single <op> <algo> <axis_label> <n> <ttl_span> <distinct> <workload> [safety_pct]\n", argv[0]);
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
    double safety_pct = argc == 10 ? strtod(argv[9], NULL) : MEM_SAFETY_PCT;

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

    params_t params = {n, ttl_span, distinct, workload};

    char n_str[32];
    human_readable_n(n, n_str);
    double need_gb, budget_gb;
    if (memory_ok(algo->name, n, &need_gb, &budget_gb)) {
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
        else if (!strcmp(argv[1], "inflection")) { run_inflection(dir); }
        else if (!strcmp(argv[1], "huge")) { run_sweeps(dir, true); }
        else if (!strcmp(argv[1], "single")) { return run_single(argc,  argv); }
        else if (!strcmp(argv[1], "all")) {
            run_sweeps(dir, false);
            run_sweeps(dir, true);
            run_distribution(dir);
            run_inflection(dir);

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
