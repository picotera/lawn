"""Lawn timer adapter: O(1) insert/delete, no tombstones.

Lawn buckets timers by their TTL value, not by absolute expiry. Within one TTL
bucket, timers arrive in expiration order (same TTL, monotonic clock), so each
bucket is self-sorted and its head is the earliest. Using an insertion-ordered
dict per bucket gives O(1) head-read, O(1) delete-by-id, and no tombstones.

`t` = number of distinct live TTL values. A tick where nothing is due is O(1):
`next_expiration` (a lower bound on the earliest live expiry) short-circuits the
scan, matching the C implementation (src/lawn.c). A tick that expires timers is
O(t + expired): the due prefixes are drained and `next_expiration` recomputed
across the `t` bucket heads.
"""
from __future__ import annotations

from typing import Dict

from timerstore import register

_INF = 1 << 62  # sentinel earliest-expiry when the lawn is empty


class LawnStore:
    name = "lawn"

    def __init__(self) -> None:
        self.now = 0
        # ttl -> {timer_id: expire_tick}, insertion order == expiry order.
        self.queues: Dict[int, Dict[int, int]] = {}
        # timer_id -> ttl, for O(1) delete.
        self.id_to_ttl: Dict[int, int] = {}
        # Lower bound on the earliest live expiry; makes empty ticks O(1).
        self.next_expiration = _INF

    def start_timer(self, timer_id: int, ttl_ticks: int) -> None:
        if ttl_ticks < 1:
            raise ValueError(f"ttl_ticks must be >= 1, got {ttl_ticks}")
        q = self.queues.get(ttl_ticks)
        if q is None:
            q = self.queues[ttl_ticks] = {}
        expire = self.now + ttl_ticks
        q[timer_id] = expire
        self.id_to_ttl[timer_id] = ttl_ticks
        if expire < self.next_expiration:
            self.next_expiration = expire

    def stop_timer(self, timer_id: int) -> bool:
        ttl = self.id_to_ttl.pop(timer_id, None)
        if ttl is None:
            return False
        q = self.queues.get(ttl)
        if q is not None:
            q.pop(timer_id, None)
            if not q:
                del self.queues[ttl]  # leave the per-tick scan
        return True

    def per_tick_bookkeeping(self) -> int:
        self.now += 1
        now = self.now
        # O(1) empty tick: nothing can be due before the earliest live expiry.
        if now < self.next_expiration:
            return 0
        expired = 0
        # Snapshot so emptied buckets can be dropped during the scan.
        for ttl, q in list(self.queues.items()):
            # Queue is in expiry order: walk the due prefix, stop at the first
            # not-yet-due. Collect keys and delete them by key afterwards.
            # (Draining via repeated next(iter(q)) + del is O(due^2) because the
            # iterator must skip tombstones each step; this is O(due).)
            due = []
            for tid, exp in q.items():
                if exp > now:
                    break
                due.append(tid)
            if not due:
                continue
            if len(due) == len(q):
                del self.queues[ttl]  # whole bucket due: drop it wholesale
            else:
                for tid in due:
                    del q[tid]
            for tid in due:
                del self.id_to_ttl[tid]
            expired += len(due)
        # Recompute the earliest live expiry across the t bucket heads (each
        # bucket's first entry is its earliest). O(t), only on non-empty ticks.
        ne = _INF
        for q in self.queues.values():
            head = next(iter(q.values()))
            if head < ne:
                ne = head
        self.next_expiration = ne
        return expired

    def size(self) -> int:
        return len(self.id_to_ttl)


register("lawn", LawnStore)
