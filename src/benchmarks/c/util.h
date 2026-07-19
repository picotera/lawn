#ifndef CTS_UTIL_H
#define CTS_UTIL_H

#include <stdint.h>
#include <stddef.h>

/* workload codes */
#define WL_UNIFORM 0
#define WL_BURSTY  1
#define WL_SPREAD  2

typedef struct { uint64_t s; } rng_t;
void     rng_seed(rng_t *r, uint64_t seed);
uint64_t rng_u64(rng_t *r);
double   rng_double(rng_t *r);   /* [0,1) */

uint64_t cts_now_ns(void);

/* Fill out[0..n) with TTLs (ticks). Semantics match Python gen_ttls:
 * distinct_ttls values evenly spaced over [1,ttl_span]; uniform=random pick,
 * bursty=80% one hot value, spread=round-robin. Returns n. */
size_t gen_ttls(uint64_t *out, size_t n, uint64_t ttl_span,
                uint64_t distinct_ttls, int workload, uint64_t seed);

/* Fisher-Yates shuffle of a 0..n-1 permutation into out. */
void gen_shuffle(uint64_t *out, size_t n, uint64_t seed);

typedef struct { double mean, std, p99, max; size_t n; } agg_t;
agg_t aggregate(double *samples, size_t n);  /* sorts samples in place */

#endif /* CTS_UTIL_H */
