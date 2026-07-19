"""Run every (operation x parameter-axis) sweep and emit CSVs + PNGs.

Registered algorithms come from the ALGORITHMS registry (importing the adapter
modules populates it), so this file names no concrete algorithm. Each of the 5
measurements is swept across each of the 4 axes -> 20 graphs, plus a derived
bytes-per-timer graph on the n axis.

  python run_benchmarks.py            # full publication dataset
  python run_benchmarks.py --quick    # small smoke run
"""
from __future__ import annotations

import argparse
import os
from functools import partial

import lawn_store         # noqa: F401  (registers "lawn")
import naive_wheel_store  # noqa: F401  (registers "naivewheel")
import timerwheel_store   # noqa: F401  (registers "timerwheel")
from experiment import ExperimentManager, Point, Sweep
from measurements import MAX_OPS, MEASUREMENTS, TICKS, WINDOW
from timerstore import ALGORITHMS

# (ylabel, csv unit, log-y) per metric.
METRICS = {
    "insert": ("insert latency (ns)", "ns", True),
    "delete": ("delete latency (ns)", "ns", True),
    "tick_advance": ("per-tick latency (ns)", "ns", True),
    "expiry": ("per-tick expiry latency (ns)", "ns", True),
    "memory": ("memory (bytes)", "bytes", True),
}

BASELINE_FULL = dict(n=100_000, ttl_span=2560, distinct_ttls=100, workload="uniform")
BASELINE_QUICK = dict(n=5_000, ttl_span=2560, distinct_ttls=100, workload="uniform")

# axis -> (values, log-x)
AXES_FULL = {
    "n": ([1_000, 10_000, 100_000, 1_000_000], True),
    "ttl_span": ([256, 2560, 25600, 256000], True),
    "distinct_ttls": ([1, 10, 100, 1000, 10000], True),
    "workload": (["uniform", "bursty", "spread"], False),
}
AXES_QUICK = {
    "n": ([1_000, 5_000, 20_000], True),
    "ttl_span": ([256, 2560, 25600], True),
    "distinct_ttls": ([1, 10, 100], True),
    "workload": (["uniform", "bursty", "spread"], False),
}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--quick", action="store_true", help="small, fast smoke run")
    ap.add_argument("--results-dir", default=None, help="output directory")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    results = args.results_dir or os.path.join(here, "results")
    os.makedirs(results, exist_ok=True)

    if args.quick:
        baseline, axes = BASELINE_QUICK, AXES_QUICK
        runs, warmup = 2, 1
        cfg = dict(max_ops=2_000, ticks=500, window=50)
    else:
        baseline, axes = BASELINE_FULL, AXES_FULL
        runs, warmup = 7, 2
        cfg = dict(max_ops=MAX_OPS, ticks=TICKS, window=WINDOW)

    mgr = ExperimentManager(ALGORITHMS, runs=runs, warmup=warmup)
    print(f"algorithms: {', '.join(ALGORITHMS)} | runs={runs} warmup={warmup} "
          f"| {'quick' if args.quick else 'full'} -> {results}")

    for mname, base_fn in MEASUREMENTS.items():
        mfn = partial(base_fn, **cfg)
        ylabel, unit, logy = METRICS[mname]
        for axis, (values, logx) in axes.items():
            sweep = Sweep(axis=axis, values=values, baseline=dict(baseline))
            print(f"  {mname} vs {axis} ...", flush=True)
            points = mgr.run(mfn, sweep)
            stem = os.path.join(results, f"{mname}_{axis}")
            mgr.to_csv(points, stem + ".csv", axis, unit)
            mgr.plot(points, stem + ".png", axis, ylabel, f"{mname} vs {axis}",
                     logx=logx, logy=logy)

            if mname == "memory" and axis == "n":
                per = [Point(p.value, p.algo, p.mean / p.value, p.std / p.value,
                             p.p99 / p.value, p.maxv / p.value, p.n_samples)
                       for p in points]
                pstem = os.path.join(results, "memory_per_timer_n")
                mgr.to_csv(per, pstem + ".csv", "n", "bytes_per_timer")
                mgr.plot(per, pstem + ".png", "n", "bytes per timer",
                         "memory per timer vs n", logx=True, logy=False)

    print(f"done. CSVs + PNGs in {results}")


if __name__ == "__main__":
    main()
