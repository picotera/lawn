import pytest
import time
from lawn_timer import LawnTimer

def test_lawn_timer_initialization():
    """Test Lawn Timer initialization."""
    lawn = LawnTimer()
    assert lawn.get_size() == 0

def test_add_timer():
    """Test adding a timer."""
    lawn = LawnTimer()
    timer_id = lawn.add_timer(lambda: None, 1.0)
    assert lawn.get_size() == 1
    assert timer_id == 0

def test_remove_timer():
    """Test removing a timer."""
    lawn = LawnTimer()
    timer_id = lawn.add_timer(lambda: None, 1.0)
    assert lawn.remove_timer(timer_id) is True
    assert lawn.get_size() == 0
    assert lawn.remove_timer(timer_id) is False

def test_tick():
    """Test timer tick functionality."""
    callback_called = False
    def callback():
        nonlocal callback_called
        callback_called = True
    
    lawn = LawnTimer()
    lawn.add_timer(callback, 0.1)
    time.sleep(0.2)  # Wait for timer to expire
    lawn.tick()
    assert callback_called

def test_clear():
    """Test clearing all timers."""
    lawn = LawnTimer()
    lawn.add_timer(lambda: None, 1.0)
    lawn.add_timer(lambda: None, 2.0)
    lawn.clear()
    assert lawn.get_size() == 0

def test_timer_ordering():
    """Test that timers are processed in correct order."""
    callbacks = []
    def make_callback(value):
        def callback():
            callbacks.append(value)
        return callback
    
    lawn = LawnTimer()
    lawn.add_timer(make_callback(1), 0.2)
    lawn.add_timer(make_callback(2), 0.1)
    lawn.add_timer(make_callback(3), 0.3)
    
    time.sleep(0.4)  # Wait for all timers to expire
    lawn.tick()
    
    assert callbacks == [2, 1, 3]  # Should be processed in order of expiration 