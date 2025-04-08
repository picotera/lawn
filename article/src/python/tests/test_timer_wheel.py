import pytest
import time
from timer_wheel import TimerWheel

def test_timer_wheel_initialization():
    """Test TimerWheel initialization."""
    wheel = TimerWheel()
    assert wheel.get_size() == 0
    assert wheel.current_bucket == 0

def test_add_timer():
    """Test adding a timer."""
    wheel = TimerWheel()
    timer_id = wheel.add_timer(lambda: None, 1.0)
    assert wheel.get_size() == 1
    assert timer_id == 0

def test_remove_timer():
    """Test removing a timer."""
    wheel = TimerWheel()
    timer_id = wheel.add_timer(lambda: None, 1.0)
    assert wheel.remove_timer(timer_id) is True
    assert wheel.get_size() == 0
    assert wheel.remove_timer(timer_id) is False

def test_tick():
    """Test timer tick functionality."""
    callback_called = False
    def callback():
        nonlocal callback_called
        callback_called = True
    
    wheel = TimerWheel()
    wheel.add_timer(callback, 0.1)
    time.sleep(0.2)  # Wait for timer to expire
    wheel.tick()
    assert callback_called

def test_clear():
    """Test clearing all timers."""
    wheel = TimerWheel()
    wheel.add_timer(lambda: None, 1.0)
    wheel.add_timer(lambda: None, 2.0)
    wheel.clear()
    assert wheel.get_size() == 0 