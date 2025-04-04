import time
import random
import numpy as np
import matplotlib.pyplot as plt
import pandas as pd
from typing import List, Tuple, Callable, Generator, Dict
from src.python.timer_wheel import TimerWheel
from src.python.lawn import Lawn
import argparse
from tqdm import tqdm
import psutil
import os
import gc
import subprocess
import csv

BATCH_SIZE = 1000000

def dummy_callback():
    """Dummy callback function for timers."""
    pass

def generate_ttl() -> float:
    """Generate a random TTL between 0.1 and 10.0."""
    return random.choice(range(1, 100))/10.0

def benchmark_insertion_for_specific_ds(data_structure: object, num_timers: int, pbar: tqdm=None) -> Generator[float, None, None]:
    ttls = [generate_ttl() for _ in range(num_timers)]
    start_time = time.time()
    for ttl in ttls:
        data_structure.add_timer(dummy_callback, ttl)
    run_time = (time.time() - start_time)/num_timers
    if pbar:
        pbar.update(num_timers)
    yield run_time

def benchmark_insertion(num_timers: int, pbar: tqdm) -> Tuple[List[float], List[float]]:
    """
    Benchmark timer insertion performance.
    
    Args:
        num_timers: Number of timers to insert
        num_runs: Number of benchmark runs
        
    Returns:
        Tuple of lists containing insertion times for TimerWheel and Lawn
    """
    
    # TimerWheel benchmark
    wheel_times = list(benchmark_insertion_for_specific_ds(TimerWheel(), num_timers, pbar))
    # Lawn benchmark
    lawn_times = list(benchmark_insertion_for_specific_ds(Lawn(), num_timers, pbar))

    return wheel_times, lawn_times

def benchmark_deletion_for_specific_ds(data_structure: object, num_timers: int, pbar: tqdm) -> Generator[float, None, None]:
    timer_ids = []
    for _ in range(num_timers):
        timer_ids.append(data_structure.add_timer(dummy_callback, generate_ttl()))
    
    shuffled_timer_ids = timer_ids.copy()
    random.shuffle(shuffled_timer_ids)

    start_time = time.time()
    for timer_id in shuffled_timer_ids:
        data_structure.remove_timer(timer_id)
    run_time = (time.time() - start_time)/len(shuffled_timer_ids)
    pbar.update(len(shuffled_timer_ids))
    yield run_time



def benchmark_deletion(num_timers: int, pbar: tqdm) -> Tuple[List[float], List[float]]:
    """
    Benchmark timer deletion performance.
    
    Args:
        num_timers: Number of timers to insert and delete
        num_runs: Number of benchmark runs
        
    Returns:
        Tuple of lists containing deletion times for TimerWheel and Lawn
    """
    
    # TimerWheel benchmark
    wheel_times = list(benchmark_deletion_for_specific_ds(TimerWheel(), num_timers, pbar))

    # Lawn benchmark
    lawn_times = list(benchmark_deletion_for_specific_ds(Lawn(), num_timers, pbar))

    return wheel_times, lawn_times

def benchmark_tick_for_specific_ds(data_structure: object, num_timers: int, pbar: tqdm=None, generate_ttl: Callable=generate_ttl) -> Generator[float, None, None]:
    for _ in range(num_timers):
        data_structure.add_timer(dummy_callback, generate_ttl())
    
    while data_structure.get_size() > 0:
        start_time = time.time()
        expired = data_structure.tick()
        tick_time = time.time() - start_time
        if pbar:
            pbar.update(expired)
        time.sleep(0.01)
        yield (tick_time)

def benchmark_tick(num_timers: int, pbar: tqdm) -> Tuple[List[float], List[float]]:
    """
    Benchmark timer tick performance.
    
    Args:
        num_timers: Number of timers to process
        num_runs: Number of benchmark runs
        
    Returns:
        Tuple of lists containing tick processing times for TimerWheel and Lawn
    """


    # Lawn benchmark
    lawn_times = list(benchmark_tick_for_specific_ds(Lawn(), num_timers, pbar))

    # TimerWheel benchmark
    wheel_times = list(benchmark_tick_for_specific_ds(TimerWheel(), num_timers, pbar))
    
    
    return wheel_times, lawn_times

def plot_results(results: pd.DataFrame, title: str, ylabel: str):
    """Plot benchmark results."""
    plt.figure(figsize=(10, 6))
    plt.plot(results['num_timers'], results['wheel_mean'], 'b-', label='TimerWheel')
    plt.plot(results['num_timers'], results['lawn_mean'], 'r-', label='Lawn')
    plt.fill_between(results['num_timers'],
                     results['wheel_mean'] - results['wheel_std'],
                     results['wheel_mean'] + results['wheel_std'],
                     alpha=0.2, color='blue')
    plt.fill_between(results['num_timers'],
                     results['lawn_mean'] - results['lawn_std'],
                     results['lawn_mean'] + results['lawn_std'],
                     alpha=0.2, color='red')
    plt.title(title)
    plt.xlabel('Number of Timers')
    plt.ylabel(ylabel)
    plt.legend()
    plt.grid(True)
    plt.savefig(f'benchmark_{title.lower().replace(" ", "_")}.png')
    plt.close()

def run_benchmarks(benchmark_function: Callable, num_timers_list: List[int], benchmark_name: str, num_runs: int = 5) -> pd.DataFrame:
    """Run benchmarks and plot results."""
    results = {
        'num_timers': [],
        'wheel_mean': [],
        'wheel_std': [],
        'lawn_mean': [],
        'lawn_std': []
    }
    
    print(f"Running {benchmark_name} benchmarks...")
    total_runs = num_runs * sum(num_timers_list) * 2
    with tqdm(total=total_runs, desc=f"{benchmark_name} Progress", unit="timer") as pbar:
        for num_timers in num_timers_list: #tqdm(num_timers_list, desc=f"{benchmark_name} Progress", unit="test"):
            wheel_times = []
            lawn_times = []
    
        
            for _ in range(num_runs):
                iteration_wheel_times, iteration_lawn_times = benchmark_function(num_timers, pbar)
                wheel_times.extend(iteration_wheel_times)
                lawn_times.extend(iteration_lawn_times)
            
            results['num_timers'].append(num_timers)
            results['wheel_mean'].append(np.mean(wheel_times))
            results['wheel_std'].append(np.std(wheel_times))
            results['lawn_mean'].append(np.mean(lawn_times))
            results['lawn_std'].append(np.std(lawn_times))
        
    df = pd.DataFrame(results)
    plot_results(df, f'{benchmark_name} Performance', 'Time (seconds)')
    return df

def measure_memory_usage(data_structure: object, num_timers: int) -> Tuple[float, float]:
    """
    Measure memory usage before and after adding timers.
    
    Args:
        data_structure: Timer implementation to test
        num_timers: Number of timers to add
        
    Returns:
        Tuple of (initial_memory, final_memory) in MB
    """
    process = psutil.Process(os.getpid())
    gc.collect()
    initial_memory = process.memory_info().rss / 1024 / 1024  # MB
    
    for _ in range(num_timers):
        data_structure.add_timer(dummy_callback, generate_ttl())
    
    gc.collect()
    final_memory = process.memory_info().rss / 1024 / 1024  # MB
    
    return initial_memory, final_memory

def benchmark_memory_usage(num_timers_list: List[int], num_runs: int = 5) -> pd.DataFrame:
    """Benchmark memory usage for both implementations."""
    results = {
        'num_timers': [],
        'wheel_initial_mem': [],
        'wheel_final_mem': [],
        'lawn_initial_mem': [],
        'lawn_final_mem': []
    }
    
    print("Running memory usage benchmarks...")
    for num_timers in num_timers_list:
        wheel_initial = []
        wheel_final = []
        lawn_initial = []
        lawn_final = []
        
        for _ in range(num_runs):
            # TimerWheel
            wheel = TimerWheel()
            init_mem, final_mem = measure_memory_usage(wheel, num_timers)
            wheel_initial.append(init_mem)
            wheel_final.append(final_mem)
            
            # Lawn
            lawn = Lawn()
            init_mem, final_mem = measure_memory_usage(lawn, num_timers)
            lawn_initial.append(init_mem)
            lawn_final.append(final_mem)
        
        results['num_timers'].append(num_timers)
        results['wheel_initial_mem'].append(np.mean(wheel_initial))
        results['wheel_final_mem'].append(np.mean(wheel_final))
        results['lawn_initial_mem'].append(np.mean(lawn_initial))
        results['lawn_final_mem'].append(np.mean(lawn_final))
    
    df = pd.DataFrame(results)
    plot_memory_results(df)
    return df

def plot_memory_results(results: pd.DataFrame):
    """Plot memory usage results."""
    plt.figure(figsize=(10, 6))
    plt.plot(results['num_timers'], results['wheel_final_mem'] - results['wheel_initial_mem'], 
             'b-', label='TimerWheel Memory Usage')
    plt.plot(results['num_timers'], results['lawn_final_mem'] - results['lawn_initial_mem'], 
             'r-', label='Lawn Memory Usage')
    plt.title('Memory Usage Comparison')
    plt.xlabel('Number of Timers')
    plt.ylabel('Memory Usage (MB)')
    plt.legend()
    plt.grid(True)
    plt.savefig('benchmark_memory_usage.png')
    plt.close()

def benchmark_workload_patterns(num_timers: int, pattern: str = 'fixed') -> Tuple[List[float], List[float]]:
    """
    Benchmark different workload patterns.
    
    Args:
        num_timers: Number of timers to process
        pattern: Workload pattern ('fixed', 'mixed', 'burst', 'uniform')
        
    Returns:
        Tuple of lists containing processing times for TimerWheel and Lawn
    """
    def generate_ttl_pattern(pattern: str) -> Callable:
        if pattern == 'fixed':
            return lambda: 1.0  # Fixed 1 second TTL
        elif pattern == 'mixed':
            return lambda: random.choice([0.1, 0.5, 1.0, 2.0, 5.0])  # Mixed TTLs
        elif pattern == 'burst':
            return lambda: random.choice([0.1, 0.1, 0.1, 1.0, 5.0])  # Bursty with many short TTLs
        elif pattern == 'uniform':
            return lambda: random.uniform(0.1, 10.0)  # Uniformly distributed TTLs
        return generate_ttl
    
    wheel_times = list(benchmark_tick_for_specific_ds(TimerWheel(), num_timers, generate_ttl=generate_ttl_pattern(pattern)))
    lawn_times = list(benchmark_tick_for_specific_ds(Lawn(), num_timers, generate_ttl=generate_ttl_pattern(pattern)))
    
    return wheel_times, lawn_times

def benchmark_concurrent_access(num_timers: int, num_threads: int = 4) -> Tuple[List[float], List[float]]:
    """
    Benchmark concurrent access patterns.
    
    Args:
        num_timers: Number of timers per thread
        num_threads: Number of concurrent threads
        
    Returns:
        Tuple of lists containing processing times for TimerWheel and Lawn
    """
    import threading
    
    def worker(data_structure, num_timers, results):
        results.extend(list(benchmark_tick_for_specific_ds(data_structure, num_timers)))
    
    wheel_times = []
    lawn_times = []
    
    # TimerWheel benchmark
    wheel = TimerWheel()
    threads = []
    wheel_results = []
    for _ in range(num_threads):
        t = threading.Thread(target=worker, args=(wheel, num_timers, wheel_results))
        threads.append(t)
        t.start()
    for t in threads:
        t.join()
    wheel_times.extend(wheel_results)
    
    # Lawn benchmark
    lawn = Lawn()
    threads = []
    lawn_results = []
    for _ in range(num_threads):
        t = threading.Thread(target=worker, args=(lawn, num_timers, lawn_results))
        threads.append(t)
        t.start()
    for t in threads:
        t.join()
    lawn_times.extend(lawn_results)
    
    return wheel_times, lawn_times

def measure_cache_behavior(data_structure: object, num_timers: int) -> Dict[str, float]:
    """
    Measure cache behavior using perf events.
    
    Args:
        data_structure: Timer implementation to test
        num_timers: Number of timers to process
        
    Returns:
        Dictionary of cache metrics
    """
    def run_perf_command(command: str) -> float:
        try:
            result = subprocess.run(command, shell=True, capture_output=True, text=True)
            return float(result.stdout.strip())
        except:
            return 0.0
    
    # Add timers
    for _ in range(num_timers):
        data_structure.add_timer(dummy_callback, generate_ttl())
    
    # Measure cache behavior during tick operations
    cache_metrics = {
        'L1_dcache_loads': run_perf_command(f'perf stat -e L1-dcache-loads -x, python -c "import sys; from {data_structure.__class__.__module__} import {data_structure.__class__.__name__}; ds = {data_structure.__class__.__name__}(); [ds.tick() for _ in range(100)]"'),
        'L1_dcache_misses': run_perf_command(f'perf stat -e L1-dcache-load-misses -x, python -c "import sys; from {data_structure.__class__.__module__} import {data_structure.__class__.__name__}; ds = {data_structure.__class__.__name__}(); [ds.tick() for _ in range(100)]"'),
        'LLC_loads': run_perf_command(f'perf stat -e LLC-loads -x, python -c "import sys; from {data_structure.__class__.__module__} import {data_structure.__class__.__name__}; ds = {data_structure.__class__.__name__}(); [ds.tick() for _ in range(100)]"'),
        'LLC_misses': run_perf_command(f'perf stat -e LLC-load-misses -x, python -c "import sys; from {data_structure.__class__.__module__} import {data_structure.__class__.__name__}; ds = {data_structure.__class__.__name__}(); [ds.tick() for _ in range(100)]"')
    }
    
    return cache_metrics

def benchmark_cache_behavior(num_timers_list: List[int]) -> pd.DataFrame:
    """Benchmark cache behavior for both implementations."""
    results = {
        'num_timers': [],
        'wheel_l1_hit_rate': [],
        'wheel_llc_hit_rate': [],
        'lawn_l1_hit_rate': [],
        'lawn_llc_hit_rate': []
    }
    
    print("Running cache behavior benchmarks...")
    total_tests = len(num_timers_list) * 2  # Two implementations
    with tqdm(total=total_tests, desc="Cache Behavior Progress", unit="test") as pbar:
        for num_timers in num_timers_list:
            # TimerWheel
            wheel = TimerWheel()
            wheel_metrics = measure_cache_behavior(wheel, num_timers)
            wheel_l1_hit_rate = 1 - (wheel_metrics['L1_dcache_misses'] / wheel_metrics['L1_dcache_loads']) if wheel_metrics['L1_dcache_loads'] > 0 else 0
            wheel_llc_hit_rate = 1 - (wheel_metrics['LLC_misses'] / wheel_metrics['LLC_loads']) if wheel_metrics['LLC_loads'] > 0 else 0
            pbar.update(1)
            
            # Lawn
            lawn = Lawn()
            lawn_metrics = measure_cache_behavior(lawn, num_timers)
            lawn_l1_hit_rate = 1 - (lawn_metrics['L1_dcache_misses'] / lawn_metrics['L1_dcache_loads']) if lawn_metrics['L1_dcache_loads'] > 0 else 0
            lawn_llc_hit_rate = 1 - (lawn_metrics['LLC_misses'] / lawn_metrics['LLC_loads']) if lawn_metrics['LLC_loads'] > 0 else 0
            pbar.update(1)
            
            results['num_timers'].append(num_timers)
            results['wheel_l1_hit_rate'].append(wheel_l1_hit_rate)
            results['wheel_llc_hit_rate'].append(wheel_llc_hit_rate)
            results['lawn_l1_hit_rate'].append(lawn_l1_hit_rate)
            results['lawn_llc_hit_rate'].append(lawn_llc_hit_rate)
    
    df = pd.DataFrame(results)
    plot_cache_results(df)
    return df

def plot_cache_results(results: pd.DataFrame):
    """Plot cache behavior results."""
    plt.figure(figsize=(12, 6))
    
    plt.subplot(1, 2, 1)
    plt.plot(results['num_timers'], results['wheel_l1_hit_rate'], 'b-', label='TimerWheel L1')
    plt.plot(results['num_timers'], results['lawn_l1_hit_rate'], 'r-', label='Lawn L1')
    plt.title('L1 Cache Hit Rate')
    plt.xlabel('Number of Timers')
    plt.ylabel('Hit Rate')
    plt.legend()
    plt.grid(True)
    
    plt.subplot(1, 2, 2)
    plt.plot(results['num_timers'], results['wheel_llc_hit_rate'], 'b-', label='TimerWheel LLC')
    plt.plot(results['num_timers'], results['lawn_llc_hit_rate'], 'r-', label='Lawn LLC')
    plt.title('LLC Cache Hit Rate')
    plt.xlabel('Number of Timers')
    plt.ylabel('Hit Rate')
    plt.legend()
    plt.grid(True)
    
    plt.tight_layout()
    plt.savefig('benchmark_cache_behavior.png')
    plt.close()

def benchmark_numa_awareness(num_timers: int, num_nodes: int = 2) -> Tuple[List[float], List[float]]:
    """
    Benchmark NUMA-aware performance.
    
    Args:
        num_timers: Number of timers per NUMA node
        num_nodes: Number of NUMA nodes to test
        
    Returns:
        Tuple of lists containing processing times for TimerWheel and Lawn
    """
    def run_on_node(node: int, data_structure: object, num_timers: int) -> float:
        try:
            # Set CPU affinity for the current process
            os.sched_setaffinity(0, {node})
            
            return np.mean(list(benchmark_tick_for_specific_ds(data_structure, num_timers)))
        except:
            return 0.0
    
    wheel_times = []
    lawn_times = []
    
    total_tests = num_nodes * 2  # Two implementations
    with tqdm(total=total_tests, desc="NUMA Test Progress", unit="node") as pbar:
        for node in range(num_nodes):
            # TimerWheel
            wheel = TimerWheel()
            wheel_time = run_on_node(node, wheel, num_timers)
            wheel_times.append(wheel_time)
            pbar.update(1)
            
            # Lawn
            lawn = Lawn()
            lawn_time = run_on_node(node, lawn, num_timers)
            lawn_times.append(lawn_time)
            pbar.update(1)
    
    return wheel_times, lawn_times

def simulate_real_workload(data_structure: object, duration: float = 60.0, pbar: tqdm = None) -> Dict[str, float]:
    """
    Simulate a real-world workload with mixed operations.
    
    Args:
        data_structure: Timer implementation to test
        duration: Duration of the simulation in seconds
        pbar: Optional progress bar
        
    Returns:
        Dictionary of performance metrics
    """
    metrics = {
        'insertions': 0,
        'deletions': 0,
        'ticks': 0,
        'max_latency': 0.0,
        'avg_latency': 0.0
    }
    
    timer_ids = []
    start_time = time.time()
    last_tick = start_time
    last_progress = 0
    
    while time.time() - start_time < duration:
        # Update progress bar every second
        current_progress = int((time.time() - start_time) / duration * 100)
        if pbar and current_progress > last_progress:
            pbar.update(current_progress - last_progress)
            last_progress = current_progress
        
        # Simulate mixed operations
        operation = random.random()
        
        if operation < 0.4:  # 40% insertions
            start_op = time.time()
            timer_id = data_structure.add_timer(dummy_callback, generate_ttl())
            timer_ids.append(timer_id)
            latency = time.time() - start_op
            metrics['insertions'] += 1
            metrics['max_latency'] = max(metrics['max_latency'], latency)
            metrics['avg_latency'] += latency
            
        elif operation < 0.7 and timer_ids:  # 30% deletions
            start_op = time.time()
            timer_id = random.choice(timer_ids)
            data_structure.remove_timer(timer_id)
            timer_ids.remove(timer_id)
            latency = time.time() - start_op
            metrics['deletions'] += 1
            metrics['max_latency'] = max(metrics['max_latency'], latency)
            metrics['avg_latency'] += latency
            
        else:  # 30% ticks
            if time.time() - last_tick >= 0.1:  # Tick every 100ms
                start_op = time.time()
                expired = data_structure.tick()
                latency = time.time() - start_op
                metrics['ticks'] += 1
                metrics['max_latency'] = max(metrics['max_latency'], latency)
                metrics['avg_latency'] += latency
                last_tick = time.time()
    
    if pbar:
        pbar.update(100 - last_progress)  # Ensure progress bar reaches 100%
    
    total_ops = metrics['insertions'] + metrics['deletions'] + metrics['ticks']
    if total_ops > 0:
        metrics['avg_latency'] /= total_ops
    
    return metrics

def benchmark_stability(duration: float = 300.0) -> Dict[str, Dict[str, float]]:
    """
    Run long-term stability test.
    
    Args:
        duration: Duration of the test in seconds
        
    Returns:
        Dictionary containing stability metrics for both implementations
    """
    print(f"Running stability test for {duration} seconds...")
    
    with tqdm(total=200, desc="Stability Test Progress", unit="%") as pbar:
        wheel_metrics = simulate_real_workload(TimerWheel(), duration, pbar)
        lawn_metrics = simulate_real_workload(Lawn(), duration, pbar)
    
    print("\nStability Test Results:")
    print("\nTimerWheel:")
    print(f"Total Operations: {wheel_metrics['insertions'] + wheel_metrics['deletions'] + wheel_metrics['ticks']}")
    print(f"Insertions: {wheel_metrics['insertions']}")
    print(f"Deletions: {wheel_metrics['deletions']}")
    print(f"Ticks: {wheel_metrics['ticks']}")
    print(f"Max Latency: {wheel_metrics['max_latency']:.6f}s")
    print(f"Average Latency: {wheel_metrics['avg_latency']:.6f}s")
    
    print("\nLawn:")
    print(f"Total Operations: {lawn_metrics['insertions'] + lawn_metrics['deletions'] + lawn_metrics['ticks']}")
    print(f"Insertions: {lawn_metrics['insertions']}")
    print(f"Deletions: {lawn_metrics['deletions']}")
    print(f"Ticks: {lawn_metrics['ticks']}")
    print(f"Max Latency: {lawn_metrics['max_latency']:.6f}s")
    print(f"Average Latency: {lawn_metrics['avg_latency']:.6f}s")
    
    return {
        'wheel': wheel_metrics,
        'lawn': lawn_metrics
    }

def export_to_csv(all_results: Dict[str, pd.DataFrame], filename: str) -> None:
    """
    Export all benchmark results to a CSV file.
    
    Args:
        all_results: Dictionary containing all benchmark results
        filename: Output CSV filename
    """
    # Create a writer object
    with open(filename, 'w', newline='') as csvfile:
        writer = csv.writer(csvfile)
        
        # Write header
        writer.writerow(['Benchmark Type', 'Metric', 'Value'])
        
        # Process each benchmark type
        for benchmark_type, data in all_results.items():
            if isinstance(data, pd.DataFrame):
                # For DataFrame results (insertion, deletion, tick, memory, cache)
                for _, row in data.iterrows():
                    for col in data.columns:
                        if col != 'num_timers':  # Skip the num_timers column
                            writer.writerow([benchmark_type, f"{col}_{row['num_timers']}", row[col]])
            elif isinstance(data, dict):
                if benchmark_type == 'workload':
                    # For workload patterns
                    for pattern, metrics in data.items():
                        for metric, value in metrics.items():
                            writer.writerow([benchmark_type, f"{pattern}_{metric}", value])
                elif benchmark_type == 'stability':
                    # For stability test
                    for impl, metrics in data.items():
                        for metric, value in metrics.items():
                            writer.writerow([benchmark_type, f"{impl}_{metric}", value])
                else:
                    # For other dictionary results (concurrent, numa)
                    for metric, value in data.items():
                        writer.writerow([benchmark_type, metric, value])
    
    print(f"Benchmark data exported to {filename}")

def main():
    """Run selected benchmarks and generate plots."""
    parser = argparse.ArgumentParser(description='Run timer benchmarks.')
    parser.add_argument('--insertion', action='store_true', help='Run insertion benchmarks')
    parser.add_argument('--deletion', action='store_true', help='Run deletion benchmarks')
    parser.add_argument('--tick', action='store_true', help='Run tick benchmarks')
    parser.add_argument('--memory', action='store_true', help='Run memory usage benchmarks')
    parser.add_argument('--workload', action='store_true', help='Run workload pattern benchmarks')
    parser.add_argument('--concurrent', action='store_true', help='Run concurrent access benchmarks')
    parser.add_argument('--cache', action='store_true', help='Run cache behavior benchmarks')
    parser.add_argument('--numa', action='store_true', help='Run NUMA awareness benchmarks')
    parser.add_argument('--stability', action='store_true', help='Run stability test')
    parser.add_argument('--duration', type=float, default=3600.0, help='Duration of stability test in seconds')
    parser.add_argument('--export-csv', type=str, help='Export all benchmark data to CSV file')
    
    args = parser.parse_args()

    if not any([args.insertion, args.deletion, args.tick, args.memory, args.workload, 
                args.concurrent, args.cache, args.numa, args.stability]):
        print("No specific benchmark selected. Running all benchmarks.")
        args.insertion = True
        args.deletion = True
        args.tick = True
        args.memory = True
        args.workload = True
        args.concurrent = True
        args.cache = True
        args.numa = True
        args.stability = True

    num_timers_list = [100, 1000, 10000, 100000, 1000000, 10000000]
    
    # Dictionary to store all benchmark results for CSV export
    all_results = {}
    
    if args.insertion:
        results = run_benchmarks(benchmark_insertion, num_timers_list, 'insertion')
        if args.export_csv:
            all_results['insertion'] = results

    if args.deletion:
        results = run_benchmarks(benchmark_deletion, num_timers_list, 'deletion')
        if args.export_csv:
            all_results['deletion'] = results

    if args.tick:
        results = run_benchmarks(benchmark_tick, num_timers_list, 'tick')
        if args.export_csv:
            all_results['tick'] = results
        
    if args.memory:
        results = benchmark_memory_usage(num_timers_list)
        if args.export_csv:
            all_results['memory'] = results
        
    if args.workload:
        patterns = ['fixed', 'mixed', 'burst', 'uniform']
        workload_results = {}
        for pattern in patterns:
            print(f"\nRunning {pattern} workload pattern benchmarks...")
            wheel_times, lawn_times = benchmark_workload_patterns(100000, pattern)
            print(f"TimerWheel {pattern} pattern: {np.mean(wheel_times):.6f}s")
            print(f"Lawn {pattern} pattern: {np.mean(lawn_times):.6f}s")
            workload_results[pattern] = {
                'wheel_mean': np.mean(wheel_times),
                'wheel_std': np.std(wheel_times),
                'lawn_mean': np.mean(lawn_times),
                'lawn_std': np.std(lawn_times)
            }
        if args.export_csv:
            all_results['workload'] = workload_results
            
    if args.concurrent:
        print("\nRunning concurrent access benchmarks...")
        wheel_times, lawn_times = benchmark_concurrent_access(100000)
        print(f"TimerWheel concurrent: {np.mean(wheel_times):.6f}s")
        print(f"Lawn concurrent: {np.mean(lawn_times):.6f}s")
        if args.export_csv:
            all_results['concurrent'] = {
                'wheel_mean': np.mean(wheel_times),
                'wheel_std': np.std(wheel_times),
                'lawn_mean': np.mean(lawn_times),
                'lawn_std': np.std(lawn_times)
            }

    if args.cache:
        results = benchmark_cache_behavior(num_timers_list)
        if args.export_csv:
            all_results['cache'] = results
        
    if args.numa:
        print("\nRunning NUMA awareness benchmarks...")
        wheel_times, lawn_times = benchmark_numa_awareness(100000)
        print(f"TimerWheel NUMA performance: {np.mean(wheel_times):.6f}s")
        print(f"Lawn NUMA performance: {np.mean(lawn_times):.6f}s")
        if args.export_csv:
            all_results['numa'] = {
                'wheel_mean': np.mean(wheel_times),
                'wheel_std': np.std(wheel_times),
                'lawn_mean': np.mean(lawn_times),
                'lawn_std': np.std(lawn_times)
            }
        
    if args.stability:
        results = benchmark_stability(args.duration)
        if args.export_csv:
            all_results['stability'] = results

    print("Benchmarks completed. Results saved as PNG files.")
    
    # Export all results to CSV if requested
    if args.export_csv and all_results:
        export_to_csv(all_results, args.export_csv)
        print(f"All benchmark data exported to {args.export_csv}")

if __name__ == "__main__":
    main() 
