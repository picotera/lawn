"""Single-level hashed timing wheel: the textbook overflow victim.

One flat ring of slots at tick resolution. A timer with TTL k lands in slot
(expire mod num_slots). This is O(1) insert / delete / tick, BUT the ring must be
at least as large as the biggest TTL it holds, or timers past the ring wrap onto
live slots and fire early. To stay correct it grows the ring (and rehashes) to
cover the largest TTL seen, so memory is O(max TTL span) regardless of how few
timers are live. That linear-in-span memory blowup is the "overflow problem"
Lawn and hierarchical wheels exist to avoid.

Included as a third baseline so the memory-vs-ttl_span graph shows the blowup a
hierarchical wheel hides.
"""
from __future__ import annotations

from typing import Dict, List, Tuple

from timerstore import register

INITIAL_SLOTS = 256


class NaiveWheelStore:
    name = "naivewheel"

    def __init__(self) -> None:
        self.now = 0
        self.num_slots = INITIAL_SLOTS
        self.slots: List[Dict[int, int]] = [{} for _ in range(self.num_slots)]
        self.id_map: Dict[int, Tuple[int, int]] = {}  # id -> (slot, expire)

    def _grow(self, min_ttl: int) -> None:
        new_slots = self.num_slots
        while new_slots <= min_ttl:
            new_slots <<= 1  # power-of-two ring that covers min_ttl
        old = self.slots
        self.num_slots = new_slots
        self.slots = [{} for _ in range(new_slots)]
        new_map: Dict[int, Tuple[int, int]] = {}
        for slot in old:  # rehash every live timer into the bigger ring
            for tid, expire in slot.items():
                idx = expire % new_slots
                self.slots[idx][tid] = expire
                new_map[tid] = (idx, expire)
        self.id_map = new_map

    def start_timer(self, timer_id: int, ttl_ticks: int) -> None:
        if ttl_ticks < 1:
            raise ValueError(f"ttl_ticks must be >= 1, got {ttl_ticks}")
        if ttl_ticks >= self.num_slots:
            self._grow(ttl_ticks)  # overflow: enlarge the ring to fit
        expire = self.now + ttl_ticks
        idx = expire % self.num_slots
        self.slots[idx][timer_id] = expire
        self.id_map[timer_id] = (idx, expire)

    def stop_timer(self, timer_id: int) -> bool:
        loc = self.id_map.pop(timer_id, None)
        if loc is None:
            return False
        idx, _expire = loc
        self.slots[idx].pop(timer_id, None)
        return True

    def per_tick_bookkeeping(self) -> int:
        self.now += 1
        idx = self.now % self.num_slots
        slot = self.slots[idx]
        if not slot:
            return 0
        # With num_slots > every live TTL, all entries in this slot are due now;
        # the filter is a cheap guard for the transient before the first grow.
        due = [tid for tid, exp in slot.items() if exp <= self.now]
        if len(due) == len(slot):
            self.slots[idx] = {}
        else:
            for tid in due:
                del slot[tid]
        for tid in due:
            del self.id_map[tid]
        return len(due)

    def size(self) -> int:
        return len(self.id_map)


register("naivewheel", NaiveWheelStore)
