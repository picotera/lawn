/* Multithreaded concurrency benchmark for the timer stores.
 *
 * Real threads (pthreads), wall-clock timing - this measures contended
 * add/delete throughput and latency, unlike the single-thread logical-clock
 * harness. Each store is wrapped in `shards` independent (store + mutex)
 * partitions; keys are routed to a shard by hash. shards=1 is a single global
 * lock (max contention); shards=threads approaches embarrassingly parallel.
 *
 * Only lawn2 / wahern / naive are used: they carry a per-store clock and shard
 * cleanly. The `lawn` adapter drives lawn.c through the global g_cts_now clock,
 * so it cannot run in independent concurrent shards (a harness limitation, not
 * an algorithm one) and is excluded here.
 *
 *   ./concurrent <impl> <threads> <shards> <ms> [window] [seed]
 * prints one CSV row: impl,threads,shards,ops,ops_per_sec,mean_ns,p50_ns,p99_ns,max_ns
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cts.h"

static uint64_t now_ns(void) {
#if defined(__APPLE__)
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

typedef struct { cts_store *store; pthread_mutex_t lock; char pad[64]; } shard_t;

static const cts_vtable *find_impl(const char *name) {
    for (int i = 0; i < cts_nalgos; i++)
        if (!strcmp(cts_algos[i]->name, name)) return cts_algos[i];
    return NULL;
}

static inline int route(uint64_t id, int nshards) {
    return (int)((id * 2654435761u) % (uint32_t)nshards);
}

typedef struct {
    const cts_vtable *vt;
    shard_t *shards; int nshards;
    int window;
    uint64_t base_id;
    volatile int *stop;
    unsigned seed;
    uint64_t ops;
    double *lat; int nlat, latcap;
} worker_arg;

static void *worker(void *p) {
    worker_arg *a = p;
    unsigned s = a->seed;
    /* Dense, bounded id range [base, base+window) reused via churn, so the
     * id-indexed adapters stay small. base_id = thread_index * window. */
    for (int i = 0; i < a->window; i++) {         /* prepopulate this thread's window */
        uint64_t id = a->base_id + (uint64_t)i;
        int sh = route(id, a->nshards);
        pthread_mutex_lock(&a->shards[sh].lock);
        a->vt->start(a->shards[sh].store, id, 1 + (rand_r(&s) % 1000));
        pthread_mutex_unlock(&a->shards[sh].lock);
    }
    uint64_t counter = 0;
    while (!*a->stop) {                            /* churn: reset one timer (stop+start same id) */
        uint64_t id = a->base_id + (counter++ % (uint64_t)a->window);
        uint64_t ttl = 1 + (rand_r(&s) % 1000);
        int sh = route(id, a->nshards);
        uint64_t t0 = now_ns();
        pthread_mutex_lock(&a->shards[sh].lock);
        a->vt->stop(a->shards[sh].store, id);
        a->vt->start(a->shards[sh].store, id, ttl);
        pthread_mutex_unlock(&a->shards[sh].lock);
        a->ops += 2;
        if (a->nlat < a->latcap && (a->ops & 127) == 0)
            a->lat[a->nlat++] = (double)(now_ns() - t0) / 2.0;
    }
    return NULL;
}

static int cmp_d(const void *x, const void *y) {
    double a = *(const double *)x, b = *(const double *)y;
    return (a > b) - (a < b);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <impl> <threads> <shards> <ms> [window] [seed]\n", argv[0]);
        return 2;
    }
    const char *impl = argv[1];
    int threads = atoi(argv[2]), shards = atoi(argv[3]), ms = atoi(argv[4]);
    int window = argc > 5 ? atoi(argv[5]) : 2000;
    unsigned seed = argc > 6 ? (unsigned)atoi(argv[6]) : 1234u;
    const cts_vtable *vt = find_impl(impl);
    if (!vt) { fprintf(stderr, "unknown impl %s\n", impl); return 2; }
    if (!strcmp(impl, "lawn") && shards != 1) {
        fprintf(stderr, "lawn uses a global clock; only shards=1 is valid\n");
        return 2;
    }

    shard_t *sh = calloc((size_t)shards, sizeof *sh);
    for (int i = 0; i < shards; i++) {
        sh[i].store = vt->create();
        pthread_mutex_init(&sh[i].lock, NULL);
    }
    volatile int stop = 0;
    pthread_t *th = calloc((size_t)threads, sizeof *th);
    worker_arg *args = calloc((size_t)threads, sizeof *args);
    for (int i = 0; i < threads; i++) {
        args[i] = (worker_arg){ .vt = vt, .shards = sh, .nshards = shards,
            .window = window, .base_id = (uint64_t)i * (uint64_t)window, .stop = &stop,
            .seed = seed + (unsigned)i, .latcap = 20000 };
        args[i].lat = malloc(sizeof(double) * (size_t)args[i].latcap);
    }
    uint64_t t_start = now_ns();
    for (int i = 0; i < threads; i++) pthread_create(&th[i], NULL, worker, &args[i]);
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
    stop = 1;
    for (int i = 0; i < threads; i++) pthread_join(th[i], NULL);
    double elapsed_s = (double)(now_ns() - t_start) / 1e9;

    uint64_t total = 0; int nl = 0;
    for (int i = 0; i < threads; i++) { total += args[i].ops; nl += args[i].nlat; }
    double *lat = malloc(sizeof(double) * (size_t)(nl ? nl : 1));
    int k = 0; double sum = 0;
    for (int i = 0; i < threads; i++)
        for (int j = 0; j < args[i].nlat; j++) { lat[k++] = args[i].lat[j]; sum += args[i].lat[j]; }
    qsort(lat, (size_t)k, sizeof(double), cmp_d);
    double mean = k ? sum / k : 0;
    double p50 = k ? lat[k / 2] : 0;
    double p99 = k ? lat[(int)(k * 0.99)] : 0;
    double mx = k ? lat[k - 1] : 0;
    printf("%s,%d,%d,%llu,%.0f,%.1f,%.1f,%.1f,%.1f\n", impl, threads, shards,
           (unsigned long long)total, total / elapsed_s, mean, p50, p99, mx);
    return 0;
}
