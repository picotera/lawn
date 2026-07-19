"""Find the inflection point in the distinct-TTL / timer-count ratio (t/N).

Lawn's per-tick cost is O(t) in the number of distinct TTLs and ~independent of
N; the wheels are O(1) per tick. Lawn wins insert and memory. So there is a
crossover in t (equivalently t/N) beyond which Lawn's per-tick penalty outweighs
its insert win. This sweeps t at several fixed N and reports the crossover from a
composite full-lifecycle metric (insert N timers, then tick until all expire),
plus the pure per-tick ratio for context.

  python inflection.py            # full
  python inflection.py --quick    # small smoke run
"""
from __future__ import annotations

import argparse
import gc
import math
import os
import random
import statistics
import time
from typing import List, Optional

import lawn_store         # noqa: F401
import timerwheel_store   # noqa: F401
from measurements import gen_ttls, measure_insert, measure_tick_advance
from timerstore import ALGORITHMS

_perf = time.perf_counter_ns
SPAN = 10_000  # fixed TTL span so ticks-to-drain is bounded; t swept within it


def measure_lifecycle(factory, *, n, ttl_span, distinct_ttls, workload, seed,
                      **_) -> List[float]:
    """Per-timer cost (ns) of inserting n timers then ticking until all drain."""
    rng = random.Random(seed)
    ttls = gen_ttls(n, ttl_span, distinct_ttls, workload, rng)
    store = factory()
    gc.disable()
    try:
        t0 = _perf()
        for i in range(n):
            store.start_timer(i, ttls[i])
        while store.size():
            store.per_tick_bookkeeping()
        elapsed = _perf() - t0
    finally:
        gc.enable()
    return [elapsed / n]


def _mean(factory, fn, *, runs, warmup, seed0, **params) -> float:
    vals: List[float] = []
    for r in range(warmup + runs):
        s = fn(factory, seed=seed0 + r, **params)
        if r >= warmup:
            vals.extend(s)
    return statistics.fmean(vals)


def _crossover(ts: List[float], ratios: List[float], level: float = 1.0) -> Optional[float]:
    """Log-interpolated t where ratio crosses `level` (None if it never does)."""
    for i in range(1, len(ts)):
        a, b = ratios[i - 1] - level, ratios[i] - level
        if a == 0:
            return ts[i - 1]
        if a * b < 0:
            lt0, lt1 = math.log(ts[i - 1]), math.log(ts[i])
            lr0, lr1 = math.log(ratios[i - 1]), math.log(ratios[i])
            frac = (math.log(level) - lr0) / (lr1 - lr0)
            return math.exp(lt0 + frac * (lt1 - lt0))
    return None


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--results-dir", default=None)
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    results = args.results_dir or os.path.join(here, "results")
    os.makedirs(results, exist_ok=True)

    if args.quick:
        n_values = [10_000, 100_000]
        t_grid = [1, 5, 20, 100, 500, 2000, 10_000]
        runs, warmup, cfg = 2, 1, dict(max_ops=2_000, ticks=500)
    else:
        n_values = [10_000, 100_000, 1_000_000]
        t_grid = [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10_000]
        runs, warmup, cfg = 3, 1, dict(max_ops=20_000, ticks=1000)

    lawn = ALGORITHMS["lawn"]
    wheel = ALGORITHMS["timerwheel"]

    rows = [("N", "t", "t_over_N", "lawn_life_ns", "wheel_life_ns", "life_ratio",
             "lawn_tick_ns", "wheel_tick_ns", "tick_ratio",
             "lawn_ins_ns", "wheel_ins_ns")]
    # For plotting: ratio curves keyed by N.
    life_curves = {}
    tick_curves = {}
    print(f"inflection sweep | N={n_values} | runs={runs} -> {results}")
    for n in n_values:
        ts = [t for t in t_grid if t <= min(n, SPAN)]
        life_r, tick_r = [], []
        for t in ts:
            p = dict(n=n, ttl_span=SPAN, distinct_ttls=t, workload="uniform")
            ll = _mean(lawn, measure_lifecycle, runs=runs, warmup=warmup,
                       seed0=100, **p)
            wl = _mean(wheel, measure_lifecycle, runs=runs, warmup=warmup,
                       seed0=100, **p)
            lt = _mean(lawn, measure_tick_advance, runs=runs, warmup=warmup,
                       seed0=200, ticks=cfg["ticks"], **p)
            wt = _mean(wheel, measure_tick_advance, runs=runs, warmup=warmup,
                       seed0=200, ticks=cfg["ticks"], **p)
            li = _mean(lawn, measure_insert, runs=runs, warmup=warmup,
                       seed0=300, max_ops=cfg["max_ops"], **p)
            wi = _mean(wheel, measure_insert, runs=runs, warmup=warmup,
                       seed0=300, max_ops=cfg["max_ops"], **p)
            life_r.append(ll / wl)
            tick_r.append(lt / wt)
            rows.append((n, t, t / n, f"{ll:.1f}", f"{wl:.1f}", f"{ll/wl:.3f}",
                         f"{lt:.1f}", f"{wt:.1f}", f"{lt/wt:.3f}",
                         f"{li:.1f}", f"{wi:.1f}"))
            print(f"  N={n:>8} t={t:>6} t/N={t/n:.2e} "
                  f"life_ratio={ll/wl:5.2f} tick_ratio={lt/wt:7.1f}")
        life_curves[n] = (ts, life_r)
        tick_curves[n] = (ts, tick_r)
        xover = _crossover(ts, life_r, 1.0)
        if xover:
            print(f"  --> N={n}: lifecycle crossover at t*={xover:.0f} "
                  f"(t/N={xover/n:.2e}); Lawn wins below, wheel above")
        else:
            side = "wins" if life_r[-1] < 1 else "loses"
            print(f"  --> N={n}: no lifecycle crossover in range; "
                  f"Lawn {side} throughout (ratio {life_r[-1]:.2f} at t={ts[-1]})")

    with open(os.path.join(results, "inflection.csv"), "w") as f:
        for row in rows:
            f.write(",".join(str(c) for c in row) + "\n")

    _plot_ratio(life_curves, os.path.join(results, "inflection_lifecycle.png"),
                "full-lifecycle cost ratio (Lawn / wheel)",
                "Lawn vs Timer Wheel: lifecycle cost by distinct-TTL count")
    _plot_ratio(tick_curves, os.path.join(results, "inflection_tick.png"),
                "per-tick cost ratio (Lawn / wheel)",
                "Lawn vs Timer Wheel: per-tick cost by distinct-TTL count")
    print(f"done -> inflection.csv + 2 PNGs in {results}")


def _plot_ratio(curves, path, ylabel, title) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(7.5, 4.8))
    for n, (ts, ratios) in curves.items():
        ax.plot(ts, ratios, marker="o", label=f"N={n:,}")
    ax.axhline(1.0, color="grey", linestyle=":", label="parity (Lawn = wheel)")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("distinct TTL count  t")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.legend()
    ax.grid(True, which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig(path, dpi=120)
    plt.close(fig)


if __name__ == "__main__":
    main()
