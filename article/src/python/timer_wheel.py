from typing import List, Callable, Dict
import time
from dataclasses import dataclass
from collections import deque

@dataclass
class Timer:
    """Represents a timer with its callback and expiration time."""
    callback: Callable
    expiration_time: float
    timer_id: int

class TimerWheel:
    """Implementation of a TimerWheel data structure for efficient timer management."""
    
    def __init__(self, num_buckets: int = 256, tick_size: float = 0.1):
        """
        Initialize the TimerWheel.
        
        Args:
            num_buckets: Number of buckets in the wheel (default: 256)
            tick_size: Size of each tick in seconds (default: 0.1)
        """
        self.num_buckets = num_buckets
        self.tick_size = tick_size
        self.wheel: List[deque] = [deque() for _ in range(num_buckets)]
        self.current_bucket = 0
        self.timer_id_counter = 0
        self.timer_registry: Dict[int, Timer] = {}
        
    def _get_bucket_index(self, expiration_time: float) -> int:
        """Calculate the bucket index for a given expiration time."""
        current_time = time.time()
        ticks = int((expiration_time - current_time) / self.tick_size)
        return (self.current_bucket + ticks) % self.num_buckets
    
    def add_timer(self, callback: Callable, delay: float) -> int:
        """
        Add a new timer to the wheel.
        
        Args:
            callback: Function to call when timer expires
            delay: Delay in seconds before timer expires
            
        Returns:
            timer_id: Unique identifier for the timer
        """
        expiration_time = time.time() + delay
        timer_id = self.timer_id_counter
        self.timer_id_counter += 1
        
        timer = Timer(callback, expiration_time, timer_id)
        bucket_index = self._get_bucket_index(expiration_time)
        self.wheel[bucket_index].append(timer)
        self.timer_registry[timer_id] = timer
        
        return timer_id
    
    def remove_timer(self, timer_id: int) -> bool:
        """
        Remove a timer from the wheel.
        
        Args:
            timer_id: ID of the timer to remove
            
        Returns:
            bool: True if timer was found and removed, False otherwise
        """
        if timer_id in self.timer_registry:
            try:
                del self.timer_registry[timer_id]
            except KeyError:
                pass
        return True
    
    def tick(self) -> int:
        current_time = time.time()
        total_expired = 0

        """Process expired timers in the current bucket."""
        current_bucket = self.wheel[self.current_bucket]
        while current_bucket:
            try:
                timer = current_bucket.popleft()
            except IndexError:
                break
            if current_time >= timer.expiration_time:
                if timer.timer_id in self.timer_registry:
                    timer.callback()
                    total_expired += 1
                    try:    
                        del self.timer_registry[timer.timer_id]
                    except KeyError:
                        pass
            else:
                # Timer hasn't expired yet, add it to the correct bucket
                bucket_index = self._get_bucket_index(timer.expiration_time)
                self.wheel[bucket_index].append(timer)

        
        self.current_bucket = (self.current_bucket + 1) % self.num_buckets
        return total_expired
    
    def get_size(self) -> int:
        """Return the current number of active timers."""
        return len(self.timer_registry.keys())
    
    def clear(self) -> None:
        """Remove all timers from the wheel."""
        for bucket in self.wheel:
            bucket.clear()
        self.timer_registry.clear()