import pytest
from benchmarks import (
    benchmark_insertion,
    benchmark_deletion,
    benchmark_tick
)

def test_benchmark_insertion():
    """Test insertion benchmarking."""
    wheel_times, lawn_times = benchmark_insertion(100, num_runs=2)
    assert len(wheel_times) == 2
    assert len(lawn_times) == 2
    assert all(t > 0 for t in wheel_times)
    assert all(t > 0 for t in lawn_times)

def test_benchmark_deletion():
    """Test deletion benchmarking."""
    wheel_times, lawn_times = benchmark_deletion(100, num_runs=2)
    assert len(wheel_times) == 2
    assert len(lawn_times) == 2
    assert all(t > 0 for t in wheel_times)
    assert all(t > 0 for t in lawn_times)

def test_benchmark_tick():
    """Test tick benchmarking."""
    wheel_times, lawn_times = benchmark_tick(100, num_runs=2)
    assert len(wheel_times) == 2
    assert len(lawn_times) == 2
    assert all(t > 0 for t in wheel_times)
    assert all(t > 0 for t in lawn_times)

def test_benchmark_scaling():
    """Test that benchmarks scale with number of timers."""
    small_wheel_times, small_lawn_times = benchmark_insertion(100, num_runs=2)
    large_wheel_times, large_lawn_times = benchmark_insertion(1000, num_runs=2)
    
    # Larger number of timers should take longer
    assert sum(large_wheel_times) > sum(small_wheel_times)
    assert sum(large_lawn_times) > sum(small_lawn_times) 