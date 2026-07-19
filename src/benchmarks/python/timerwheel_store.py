"""Hierarchical cascading timer wheel: the state-of-the-art baseline.

Linux-kernel-style multi-level wheel. Level 0 has 256 slots at tick resolution;
each higher level has 64 slots covering 64x the range of the one below. Levels
are created lazily to cover the largest live TTL, so a bigger TTL span costs
extra levels (memory ~ log(span)) and periodic cascades (tick-latency spikes)
rather than a fixed-size wheel that silently overflows. That is the honest
comparison Lawn is measured against.

Each slot is a dict {timer_id: expire_tick} for O(1) eager delete and no
tombstones (symmetric with the Lawn adapter, so memory numbers are comparable).
"""
from __future__ import annotations

from typing import Dict, List, Tuple

from timerstore import register

TVR_BITS = 8           # level 0: 256 slots at 1-tick resolution
TVN_BITS = 6           # levels 1+: 64 slots each
LVL0_SIZE = 1 << TVR_BITS
LVL0_MASK = LVL0_SIZE - 1
LVLN_SIZE = 1 << TVN_BITS
LVLN_MASK = LVLN_SIZE - 1


class TimerWheelStore:
    name = "timerwheel"

    def __init__(self) -> None:
        self.now = 0
        # levels[0] has 256 slots; levels[1:] have 64 slots. Each slot: dict.
        self.levels: List[List[Dict[int, int]]] = [[{} for _ in range(LVL0_SIZE)]]
        self.id_map: Dict[int, Tuple[int, int]] = {}

    def _locate(self, expire: int) -> Tuple[int, int]:
        delta = expire - self.now
        if delta < 1:
            # due now or past: fire on the slot being drained this tick
            return 0, self.now & LVL0_MASK
        if delta < LVL0_SIZE:
            return 0, expire & LVL0_MASK
        level = 1
        while delta >= (1 << (TVR_BITS + level * TVN_BITS)):
            level += 1
        shift = TVR_BITS + (level - 1) * TVN_BITS
        return level, (expire >> shift) & LVLN_MASK

    def _ensure_level(self, level: int) -> None:
        while len(self.levels) <= level:
            self.levels.append([{} for _ in range(LVLN_SIZE)])

    def _add(self, timer_id: int, expire: int) -> None:
        level, slot = self._locate(expire)
        self._ensure_level(level)
        self.levels[level][slot][timer_id] = expire
        self.id_map[timer_id] = (level, slot)

    def start_timer(self, timer_id: int, ttl_ticks: int) -> None:
        if ttl_ticks < 1:
            raise ValueError(f"ttl_ticks must be >= 1, got {ttl_ticks}")
        self._add(timer_id, self.now + ttl_ticks)

    def stop_timer(self, timer_id: int) -> bool:
        loc = self.id_map.pop(timer_id, None)
        if loc is None:
            return False
        level, slot = loc
        self.levels[level][slot].pop(timer_id, None)
        return True

    def _cascade(self, level: int) -> None:
        if level >= len(self.levels):
            return
        shift = TVR_BITS + (level - 1) * TVN_BITS
        idx = (self.now >> shift) & LVLN_MASK
        slot = self.levels[level][idx]
        if slot:
            self.levels[level][idx] = {}
            for timer_id, expire in slot.items():
                self._add(timer_id, expire)  # now falls into a lower level
        if idx == 0:
            self._cascade(level + 1)

    def per_tick_bookkeeping(self) -> int:
        self.now += 1
        idx = self.now & LVL0_MASK
        if idx == 0:
            self._cascade(1)  # bottom wheel wrapped: pull the next level down
        slot = self.levels[0][idx]
        expired = len(slot)
        if expired:
            for timer_id in slot:
                del self.id_map[timer_id]
            self.levels[0][idx] = {}
        return expired

    def size(self) -> int:
        return len(self.id_map)


register("timerwheel", TimerWheelStore)
