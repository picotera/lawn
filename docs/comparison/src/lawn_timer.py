from typing import Dict, Callable, NamedTuple
import time
from collections import deque


class LawnRecord(NamedTuple):
    """Represents a timer with its callback and expiration time."""
    expiration_time: float
    timer_id: int

class Lawn:
    """Implementation of a Lawn Timer data structure for efficient timer management."""
    
    def __init__(self):
        """Initialize the Lawn Timer."""
        # Use a dictionary of sets instead of lists for O(1) removal
        self.ttl_lists: Dict[float, deque[LawnRecord]] = {}  # ttl -> queue of timer ids by expiration time
        self.callback_registry: Dict[int, Callable] = {}  # timer_id -> timer
        self.timer_id_counter = 0
        
    def add_timer(self, callback: Callable, delay: float) -> int:
        """
        Add a new timer to the lawn.
        
        Args:
            callback: Function to call when timer expires
            delay: Delay in seconds before timer expires
            
        Returns:
            timer_id: Unique identifier for the timer
        """
        expiration_time = time.time() + delay
        timer_id = self.timer_id_counter
        self.timer_id_counter += 1
        
        # Add timer to the end of the list for this TTL
        if delay not in self.ttl_lists:
            self.ttl_lists[delay] = deque()
        self.ttl_lists[delay].append(LawnRecord(expiration_time=expiration_time, timer_id=timer_id))
        self.callback_registry[timer_id] = callback
        
        return timer_id
    
    def remove_timer(self, timer_id: int) -> bool:
        """
        Remove a timer from the lawn.
        
        Args:
            timer_id: ID of the timer to remove
            
        Returns:
            bool: True if timer was found and removed, False otherwise
        """
        if timer_id in self.callback_registry:
            try:
                del self.callback_registry[timer_id]
            except KeyError:
                pass
        return True
    
    def tick(self) -> int:
        """
        Process expired timers.
        
        Returns:
            int: Number of timers that expired
        """
        current_time = time.time()
        total_expired = 0
        
        # Create a list of TTLs to avoid modifying the dictionary during iteration
        ttl_list = list(self.ttl_lists.keys())
        
        for ttl in ttl_list:
            timers = self.ttl_lists[ttl]
            if not timers:
                continue
            
            # Process timers in batches
            while timers and timers[0].expiration_time <= current_time:
                timer_id = timers.popleft().timer_id
                callback = self.callback_registry.get(timer_id)
                if callback:
                    callback()
                    total_expired += 1
                    try:    
                        del self.callback_registry[timer_id]
                    except KeyError:
                        pass
        
            # If the list is empty after processing, remove the TTL entry
            # if not timers:
            #     del self.ttl_lists[ttl]
        
        return total_expired
    
    def get_size(self) -> int:
        """Return the current number of active timers."""
        return len(self.callback_registry.keys())
    
    def clear(self) -> None:
        """Remove all timers from the lawn."""
        self.ttl_lists.clear()
        self.timer_registry.clear()