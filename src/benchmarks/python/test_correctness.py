"""Correctness gate: run this (exit 0) before trusting any benchmark numbers.

A benchmark of buggy implementations is meaningless. Checks, per adapter:
- schedule vs ground truth: a timer with ttl k inserted at t=0 expires at exactly
  tick k (validates the wheel's cascade lands timers on time),
- Lawn and the wheel produce identical per-tick expiry sequences (differential),
- deleted timers never fire and size() drops (no tombstones),
- large TTLs spanning multiple wheel levels fire on the exact tick (overflow),
- stop_timer / ttl validation semantics.

Assert-based, no framework; a failed assert exits non-zero and gates the run.
"""
from __future__ import annotations

import random
from collections import Counter
from typing import Iterable, List, Tuple

from lawn_store import LawnStore
from naive_wheel_store import NaiveWheelStore
from timerwheel_store import TimerWheelStore

STORES = (LawnStore, TimerWheelStore, NaiveWheelStore)
Timers = List[Tuple[int, int]]


def check_schedule(store_cls, timers: Timers, ticks: int,
                   deleted: Iterable[int] = ()) -> None:
    store = store_cls()
    for tid, ttl in timers:
        store.start_timer(tid, ttl)
    dset = set(deleted)
    for tid in dset:
        assert store.stop_timer(tid) is True, f"{store.name}: delete of live {tid}"

    expected: Counter = Counter()
    for tid, ttl in timers:
        if tid not in dset and 1 <= ttl <= ticks:
            expected[ttl] += 1

    total = 0
    for t in range(1, ticks + 1):
        c = store.per_tick_bookkeeping()
        want = expected.get(t, 0)
        assert c == want, f"{store.name}: tick {t} expired {c}, expected {want}"
        total += c
    assert total == sum(expected.values())

    remaining = sum(1 for tid, ttl in timers if tid not in dset and ttl > ticks)
    assert store.size() == remaining, \
        f"{store.name}: size {store.size()}, expected {remaining} (tombstones?)"


def check_differential(timers: Timers, ticks: int) -> None:
    seqs = {}
    for cls in STORES:
        store = cls()
        for tid, ttl in timers:
            store.start_timer(tid, ttl)
        seqs[cls.name] = [store.per_tick_bookkeeping() for _ in range(ticks)]
    ref_name = STORES[0].name
    ref = seqs[ref_name]
    for cls in STORES[1:]:
        assert seqs[cls.name] == ref, \
            f"schedules differ between {ref_name} and {cls.name}"


def check_overflow_cascade() -> None:
    # TTLs straddling level-0 (256) and level-1 (16384) boundaries and far beyond.
    for ttl in (255, 256, 257, 16383, 16384, 16385, 20000, 300000):
        for cls in STORES:
            store = cls()
            store.start_timer(42, ttl)
            fired_at = None
            for t in range(1, ttl + 1):
                if store.per_tick_bookkeeping():
                    fired_at = t
                    break
            assert fired_at == ttl, f"{cls.name}: ttl {ttl} fired at {fired_at}"
            assert store.size() == 0


def check_semantics() -> None:
    for cls in STORES:
        store = cls()
        store.start_timer(1, 10)
        assert store.stop_timer(1) is True
        assert store.stop_timer(1) is False   # already gone
        assert store.stop_timer(999) is False  # never existed
        assert store.size() == 0
        try:
            store.start_timer(2, 0)
        except ValueError:
            pass
        else:
            raise AssertionError(f"{cls.name}: ttl_ticks=0 must raise ValueError")


def main() -> None:
    rng = random.Random(7)
    ticks = 1000
    # Some ttls exceed `ticks` to exercise still-live remainder and cascades.
    timers = [(i, rng.randint(1, 1500)) for i in range(5000)]

    for cls in STORES:
        check_schedule(cls, timers, ticks)
    check_differential(timers, ticks)
    for cls in STORES:
        check_schedule(cls, timers, ticks, deleted=list(range(0, 5000, 7)))
    check_overflow_cascade()
    check_semantics()
    print("ALL CORRECTNESS TESTS PASSED")


if __name__ == "__main__":
    main()
