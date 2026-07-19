"""One measurement function per operation.

Every function has the same shape: `measure_x(factory, *, n, ttl_span,
distinct_ttls, workload, seed, ...) -> list[float]` returning raw per-op latency
samples in nanoseconds (measure_memory returns a one-element list of bytes). The
ExperimentManager decides which parameter to sweep; these functions just run one
point. Config knobs (max_ops / ticks / window) default here and are overridden
for --quick via functools.partial in run_benchmarks.

Measurement discipline:
- workload is precomputed before the timed window,
- store construction is excluded from the timed window,
- gc is disabled around latency loops,
- callbacks are not modelled (expiry does no client work).
"""
from __future__ import annotations

import gc
import random
import time
from typing import Callable, List

from timerstore import TimerStore, measure_store_memory

MAX_OPS = 20_000   # cap on timed operations, independent of n
TICKS = 2_000      # ticks run in measure_tick_advance
WINDOW = 200       # expiry window in measure_expiry

_perf = time.perf_counter_ns

Factory = Callable[[], TimerStore]


def gen_ttls(n: int, ttl_span: int, distinct_ttls: int, workload: str,
             rng: random.Random) -> List[int]:
    """Precompute n TTLs (in ticks) over `distinct_ttls` distinct values.

    Distinct values are evenly spaced across [1, ttl_span]. Rounding at small
    spans can merge a few, which is harmless. `workload` shapes the assignment.
    """
    distinct_ttls = max(1, min(distinct_ttls, ttl_span))
    if distinct_ttls == 1:
        values = [ttl_span]
    else:
        step = ttl_span / distinct_ttls
        values = sorted({max(1, int(round((i + 1) * step)))
                         for i in range(distinct_ttls)})

    if workload == "uniform":
        return [rng.choice(values) for _ in range(n)]
    if workload == "bursty":
        hot = values[len(values) // 2]
        return [hot if rng.random() < 0.8 else rng.choice(values)
                for _ in range(n)]
    if workload == "spread":
        return [values[i % len(values)] for i in range(n)]
    raise ValueError(f"unknown workload {workload!r}")


def measure_insert(factory: Factory, *, n: int, ttl_span: int, distinct_ttls: int,
                   workload: str, seed: int, max_ops: int = MAX_OPS,
                   **_: object) -> List[float]:
    rng = random.Random(seed)
    ttls = gen_ttls(n, ttl_span, distinct_ttls, workload, rng)
    store = factory()
    timed = min(max_ops, n)
    pre = n - timed
    for i in range(pre):  # bring the store up to scale n, untimed
        store.start_timer(i, ttls[i])
    samples: List[float] = []
    gc.disable()
    try:
        for i in range(pre, n):
            t0 = _perf()
            store.start_timer(i, ttls[i])
            samples.append(_perf() - t0)
    finally:
        gc.enable()
    return samples


def measure_delete(factory: Factory, *, n: int, ttl_span: int, distinct_ttls: int,
                   workload: str, seed: int, max_ops: int = MAX_OPS,
                   **_: object) -> List[float]:
    rng = random.Random(seed)
    ttls = gen_ttls(n, ttl_span, distinct_ttls, workload, rng)
    store = factory()
    for i in range(n):
        store.start_timer(i, ttls[i])
    victims = list(range(n))
    rng.shuffle(victims)
    del victims[min(max_ops, n):]
    samples: List[float] = []
    gc.disable()
    try:
        for tid in victims:
            t0 = _perf()
            store.stop_timer(tid)
            samples.append(_perf() - t0)
    finally:
        gc.enable()
    return samples


def measure_tick_advance(factory: Factory, *, n: int, ttl_span: int,
                         distinct_ttls: int, workload: str, seed: int,
                         ticks: int = TICKS, **_: object) -> List[float]:
    """Per-tick bookkeeping with nothing due: pure clock-advance + cascade cost."""
    rng = random.Random(seed)
    ttls = gen_ttls(n, ttl_span, distinct_ttls, workload, rng)
    store = factory()
    for i in range(n):  # offset past the window so nothing expires while timing
        store.start_timer(i, ttls[i] + ticks)
    samples: List[float] = []
    gc.disable()
    try:
        for _t in range(ticks):
            t0 = _perf()
            store.per_tick_bookkeeping()
            samples.append(_perf() - t0)
    finally:
        gc.enable()
    return samples


def measure_expiry(factory: Factory, *, n: int, ttl_span: int, distinct_ttls: int,
                   workload: str, seed: int, window: int = WINDOW,
                   **_: object) -> List[float]:
    """Per-tick cost while draining timers that come due within `window` ticks."""
    rng = random.Random(seed)
    # TTLs confined to [1, window] so all fire inside the measured ticks. This
    # deliberately ignores ttl_span (expiry cost is a function of arrivals, not
    # span), so the ttl_span sweep of this metric is expected to be flat.
    ttls = gen_ttls(n, window, distinct_ttls, workload, rng)
    store = factory()
    for i in range(n):
        store.start_timer(i, ttls[i])
    samples: List[float] = []
    gc.disable()
    try:
        for _t in range(window):
            t0 = _perf()
            store.per_tick_bookkeeping()
            samples.append(_perf() - t0)
    finally:
        gc.enable()
    return samples


def measure_memory(factory: Factory, *, n: int, ttl_span: int, distinct_ttls: int,
                   workload: str, seed: int, **_: object) -> List[float]:
    """Total store footprint in bytes for n live timers (tracemalloc)."""
    rng = random.Random(seed)
    ttls = gen_ttls(n, ttl_span, distinct_ttls, workload, rng)  # not counted

    def build() -> TimerStore:
        store = factory()
        for i in range(n):
            store.start_timer(i, ttls[i])
        return store

    return [float(measure_store_memory(build))]


MEASUREMENTS = {
    "insert": measure_insert,
    "delete": measure_delete,
    "tick_advance": measure_tick_advance,
    "expiry": measure_expiry,
    "memory": measure_memory,
}
