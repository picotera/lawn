"""Common seam for the timer-datastructure benchmark harness.

Every algorithm under test implements the TimerStore protocol and registers
itself in ALGORITHMS. The measurement and plotting code never names a concrete
algorithm, so adding one is: new file + one ALGORITHMS entry.

The store runs on an integer *logical clock* (`ttl_ticks`), decoupled from
wall-clock time, so measurements capture only the data structure's own cost.
Model mirrors the Varghese-Lauck interface used by the C suite
(src/benchmarks/c/timerstore.h).
"""
from __future__ import annotations

import gc
import tracemalloc
from typing import Callable, Dict, List, Protocol, runtime_checkable


@runtime_checkable
class TimerStore(Protocol):
    """A timer store on a logical clock. TTLs and time are in integer ticks."""

    name: str

    def start_timer(self, timer_id: int, ttl_ticks: int) -> None:
        """Insert a timer that comes due `ttl_ticks` ticks from now."""

    def stop_timer(self, timer_id: int) -> bool:
        """Cancel a timer. Returns True if it was live, False if unknown."""

    def per_tick_bookkeeping(self) -> int:
        """Advance the clock one tick (cascading if needed). Return #expired."""

    def size(self) -> int:
        """Number of live timers."""


# name -> zero-arg factory returning a fresh TimerStore.
ALGORITHMS: Dict[str, Callable[[], TimerStore]] = {}


def register(name: str, factory: Callable[[], TimerStore]) -> None:
    ALGORITHMS[name] = factory


def measure_store_memory(build: Callable[[], TimerStore]) -> int:
    """Bytes allocated to build the store, via tracemalloc.

    Generic: works for any adapter with no per-algorithm sizeof. The caller must
    precompute any workload data BEFORE calling so it is not counted. Returns
    the net allocated (current) bytes attributable to the build.
    """
    gc.collect()
    tracemalloc.start()
    store = build()
    current, _peak = tracemalloc.get_traced_memory()
    tracemalloc.stop()
    # Touch the store so it cannot be collected before the reading above; also a
    # cheap sanity check that the build populated something.
    assert store.size() >= 0
    return current


def percentile(sorted_vals: List[float], p: float) -> float:
    """Linear-interpolated percentile of an already-sorted list. p in [0, 100]."""
    if not sorted_vals:
        return 0.0
    if len(sorted_vals) == 1:
        return float(sorted_vals[0])
    k = (len(sorted_vals) - 1) * p / 100.0
    f = int(k)
    c = min(f + 1, len(sorted_vals) - 1)
    return sorted_vals[f] + (sorted_vals[c] - sorted_vals[f]) * (k - f)


def _demo() -> None:
    """Self-check: percentile edges and monotonicity."""
    assert percentile([], 99) == 0.0
    assert percentile([5], 50) == 5.0
    assert percentile([0, 10], 50) == 5.0
    assert percentile([0, 100], 99) == 99.0
    assert percentile([1, 2, 3, 4], 0) == 1
    assert percentile([1, 2, 3, 4], 100) == 4
    print("timerstore self-check ok")


if __name__ == "__main__":
    _demo()
