# C timer benchmark suite

Benchmarks the real C timer implementations behind a uniform vtable
(`cts.h`), all driven by an injected integer **logical clock** so results are
deterministic and reproducible.

## Implementations

Each algorithm is an adapter in [`impl/`](impl/) implementing the `cts_vtable`
from `cts.h`; the harness (`benchmark.c`, `test.c`, `util.c`) is impl-agnostic.

- `lawn` - the repo's `src/lawn.c` (queue-map algorithm), clock injected via
  the shared logical clock in `util.c`.
- `lawn2` - an optimized, allocation-free Lawn (`src/lawn2.{c,h}`): intrusive
  handle-based nodes (no per-insert malloc, no key copy), O(1) delete by node
  pointer, open-addressing TTL->queue table, `next_expiration` O(1) empty tick.
  Same Queue-Map algorithm as `lawn` (enforced by the differential gate); beats
  the wahern wheel on every measured operation.
- `wahern` - William Ahern's `timeout.c` (tickless hierarchical wheel), the
  canonical in-the-wild baseline, compiled from `../../../article/src/c/wheel/`.
- `naive` - a single-level growing ring (the textbook overflow victim).
- `heap` - a binary heap adapter, added to test wheel-generalization beyond
  `wahern`.
- `linuxwheel` - a non-cascading wheel adapter, added for the same reason.

`heap` and `linuxwheel` are included in the correctness gate and in raw sweep
output, but are kept out of the article's main figures (see
`article/src/make_figures.py`'s `ORDER` vs `ORDER_ALL`) to keep the primary
Lawn-vs-wheel-vs-naive story uncluttered.

## Build and run

```bash
make                  # builds test and benchmark (Apple clang / gcc, C11, -Wall -Wextra)
./test                # differential correctness gate across all 6 impls
./benchmark sweeps    # 7 ops x 4 axes -> results/*.csv (main baseline, n=100K)
./benchmark huge      # same sweep at the extended baseline (n=10M)
./benchmark dist      # TTL-distribution comparison -> results/ttl_distribution.csv
./benchmark inflection # per-tick + lifecycle distinct-TTL crossover -> results/inflection.csv
                        # (one row per N/t/span_regime; span_regime is "fixed" or "scaled")
./benchmark single <op> <algo> <axis_label> <n> <ttl_span> <distinct> <workload> [safety_pct] [preload_n]
                        # one (op, algo, params) point, printed, not written to a CSV
./benchmark sweep-op <op> <axis> [huge]
                        # re-run one op x one axis sweep, e.g. `./benchmark sweep-op lifecycle n`
./benchmark all        # sweeps + huge + dist + inflection
python3 ../../../article/src/make_figures.py    # regenerate article/*.png from results/*.csv
```

With no arguments, `./benchmark` runs the main-baseline sweeps plus the
distribution comparison as a quick sanity pass.

Outputs land in `results/` (git-ignored). Timing uses the finest monotonic
clock available; on macOS that is ~41 ns quantized, so operations are timed in
micro-batches (B=256) to resolve sub-40 ns costs. Memory is measured via the
platform allocator statistics. Every heavy sweep point is guarded by
`memory_ok()`, which skips (prints `SKIP`, doesn't crash) any point estimated
to exceed a safety margin of available memory.

### Operations (`OPS[]` in `benchmark.c`)

`insert`, `delete`, `tick_advance` (empty/idle tick), `expiry`, `memory`,
`tick_scan` (a tick that actually crosses a live bucket), `lifecycle`
(a realistic mixed insert/delete/tick workload against a preloaded background
population — see the long comment above `setup_lifecycle`/`payload_lifecycle`
in `benchmark.c` for the population/replenishment model).

### Sweep axes (`PARAMETER_NAMES[]` in `benchmark.c`)

`n` (timer count / population), `ttl_span`, `distinct_ttls`, `workload`
(uniform / bursty / spread).

## Concurrency benchmark

[`concurrent/`](concurrent/) is a separate, real-pthreads, wall-clock harness
(as opposed to the logical-clock single-thread harness above) measuring
contended add/delete throughput under sharding. Only `lawn2`, `wahern`, and
`naive` participate (see the header comment in `concurrent/concurrent.c` for
why `lawn` is excluded). Build with `make -C concurrent`, run
`./concurrent/concurrent <impl> <threads> <shards> <ms> [window] [seed]`, which
prints one CSV row per invocation.

## Adding an implementation

Add an adapter `impl/<name>.c` providing a `cts_vtable`
(create/destroy/start/stop/tick/size), add it to `cts_algos[]` in `util.c`
(and to `cts.h`'s extern declarations), and add the source file to `ADAPTERS`
in the `Makefile`. The harness, gate, and plotter are impl-agnostic.
