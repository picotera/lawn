# Timer data-structure benchmarks

Two independent, algorithm-agnostic benchmark suites that compare Lawn against
timing-wheel baselines across the same parameter space, plus a distinct-TTL /
timer-count inflection study.

- [`python/`](python/) - pure-Python suite for scaling shape and jitter.
- [`c/`](c/) - C suite on the real implementations (Lawn, an
  optimized allocation-free Lawn, William Ahern's timing wheel, a naive ring).

Both suites run every algorithm behind a common adapter on an integer **logical
clock** (deterministic, wall-clock-free), measure one operation per function
(insert, delete, tick-advance, expiry, memory), and sweep four axes: timer count
N, TTL span, distinct-TTL count, and workload pattern. A differential correctness
gate (all implementations must produce identical per-tick expiry schedules) runs
before any numbers are trusted.

Start with each suite's own README for build and run instructions. Generated CSVs
and PNGs are git-ignored; regenerate them with the commands in those READMEs.
