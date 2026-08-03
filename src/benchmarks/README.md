# Timer data-structure benchmarks

An algorithm-agnostic benchmark suite, in [`c/`](c/), comparing Lawn against
timing-wheel and other baselines across the same parameter space, plus
distinct-TTL and lifecycle-cost crossover studies.

The suite runs every algorithm (Lawn, an optimized allocation-free Lawn2,
William Ahern's timing wheel, a naive ring, plus a binary heap and a
non-cascading wheel) behind a common adapter on an integer **logical clock**
(deterministic, wall-clock-free), measures one operation per function (insert,
delete, tick-advance, expiry, memory, tick-scan, lifecycle), and sweeps four
axes: timer count N, TTL span, distinct-TTL count, and workload pattern. A
differential correctness gate (all implementations must produce identical
per-tick expiry schedules) runs before any numbers are trusted.

Start with [`c/README.md`](c/README.md) for build and run instructions.
Generated CSVs and PNGs are git-ignored; regenerate them with the commands
there, then `python3 ../../article/src/make_figures.py` to rebuild the
article's figures.
