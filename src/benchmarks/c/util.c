#include "util.h"
#include <stdlib.h>
#include <math.h>
#include <time.h>

/* splitmix64 */
void rng_seed(rng_t *r, uint64_t seed) { r->s = seed + 0x9e3779b97f4a7c15ULL; }
uint64_t rng_u64(rng_t *r) {
    uint64_t z = (r->s += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
double rng_double(rng_t *r) { return (double)(rng_u64(r) >> 11) * (1.0 / 9007199254740992.0); }

uint64_t cts_now_ns(void) {
#if defined(__APPLE__)
    /* Finest monotonic source on macOS; CLOCK_MONOTONIC here is 1us-quantized. */
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

size_t gen_ttls(uint64_t *out, size_t n, uint64_t ttl_span,
                uint64_t distinct_ttls, int workload, uint64_t seed) {
    uint64_t distinct = distinct_ttls;
    if (distinct > ttl_span) distinct = ttl_span;
    if (distinct < 1) distinct = 1;

    uint64_t *values = malloc(distinct * sizeof *values);
    size_t nv = 0;
    if (distinct == 1) {
        values[nv++] = ttl_span;
    } else {
        double step = (double)ttl_span / (double)distinct;
        for (uint64_t i = 0; i < distinct; i++) {
            long v = lround((double)(i + 1) * step);
            if (v < 1) v = 1;
            if (nv == 0 || (uint64_t)v != values[nv - 1]) values[nv++] = (uint64_t)v;
        }
    }

    rng_t r; rng_seed(&r, seed);
    if (workload == WL_UNIFORM) {
        for (size_t j = 0; j < n; j++) out[j] = values[rng_u64(&r) % nv];
    } else if (workload == WL_BURSTY) {
        uint64_t hot = values[nv / 2];
        for (size_t j = 0; j < n; j++)
            out[j] = (rng_double(&r) < 0.8) ? hot : values[rng_u64(&r) % nv];
    } else { /* spread */
        for (size_t j = 0; j < n; j++) out[j] = values[j % nv];
    }
    free(values);
    return n;
}

void gen_shuffle(uint64_t *out, size_t n, uint64_t seed) {
    for (size_t i = 0; i < n; i++) out[i] = i;
    rng_t r; rng_seed(&r, seed);
    for (size_t i = n; i > 1; i--) {
        size_t j = (size_t)(rng_u64(&r) % i);
        uint64_t t = out[i - 1]; out[i - 1] = out[j]; out[j] = t;
    }
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

agg_t aggregate(double *s, size_t n) {
    agg_t a = {0, 0, 0, 0, n};
    if (n == 0) return a;
    double sum = 0;
    for (size_t i = 0; i < n; i++) sum += s[i];
    a.mean = sum / n;
    double v = 0;
    for (size_t i = 0; i < n; i++) { double d = s[i] - a.mean; v += d * d; }
    a.std = sqrt(v / n);
    qsort(s, n, sizeof(double), cmp_double);
    a.max = s[n - 1];
    if (n == 1) { a.p99 = s[0]; return a; }
    double k = (double)(n - 1) * 0.99;
    size_t f = (size_t)k, c = f + 1 < n ? f + 1 : n - 1;
    a.p99 = s[f] + (s[c] - s[f]) * (k - (double)f);
    return a;
}
