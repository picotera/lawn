# C timer benchmark suite

Benchmarks the real C timer implementations behind a uniform vtable
(`cts.h`), all driven by an injected integer **logical clock** so results are
deterministic and comparable to the Python suite.

## Implementations

Each algorithm is an adapter in [`impl/`](impl/) implementing the `cts_vtable`
from `cts.h`; the harness (`test.c`, `bench.c`, `util.c`, `registry.c`, `clock.c`)
is impl-agnostic.

- `lawn` - the repo's `src/lawn.c` (queue-map algorithm), clock injected via `clock.c`.
- `lawn2` - an optimized, allocation-free Lawn (`src/lawn2.{c,h}`): intrusive
  handle-based nodes (no per-insert malloc, no key copy), O(1) delete by node
  pointer, open-addressing TTL->queue table, `next_expiration` O(1) empty tick.
  Same Queue-Map algorithm as `lawn` (enforced by the differential gate); beats
  the wahern wheel on every measured operation.
- `wahern` - William Ahern's `timeout.c` (tickless hierarchical wheel), the
  canonical in-the-wild baseline, compiled from `../../../article/src/c/wheel/`.
- `naive` - a single-level growing ring (the textbook overflow victim).

## Build and run

```bash
make                 # builds test and bench (Apple clang / gcc, C11)
./test               # differential correctness gate across all 4 impls
./bench              # 5 operations x 4 axes + inflection -> results_c/*.csv
python plot_c.py     # results_c/*.png
```

Outputs land in `results_c/` (git-ignored). Timing uses the finest monotonic
clock available; on macOS that is ~41 ns quantized, so operations are timed in
micro-batches (B=256) to resolve sub-40 ns costs. Memory is measured via the
platform allocator statistics.

## Adding an implementation

Add an adapter `impl/<name>.c` providing a `cts_vtable`
(create/destroy/start/stop/tick/size), register it in `registry.c`, and add it to
`ADAPTERS` in the `Makefile`. The harness, gate, and plotter are impl-agnostic.
