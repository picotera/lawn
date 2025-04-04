# Lawn and TimerWheel Benchmark Library 

# Python Benchmarks

This project implements and compares two timer data structures:
1. TimerWheel
2. Lawn Timer

## Overview

This project is designed for academic research comparing the performance characteristics of different timer data structures. It provides implementations of both TimerWheel and Lawn timer algorithms, along with benchmarking tools to evaluate their performance under various conditions.


## Installation

1. Create a virtual environment:
```bash
python -m venv venv
source venv/bin/activate  # On Windows: venv\Scripts\activate
```

2. Install dependencies:
```bash
pip install -r requirements.txt
```

## Usage

Run the benchmarks:
```bash
python src/benchmarks/benchmarks.py
```

Run tests:
```bash
pytest tests/
```

## Implementation Details

### TimerWheel
The TimerWheel implementation uses a circular buffer of buckets, where each bucket contains timers that expire at the same time. This provides O(1) insertion and deletion operations for timers.

### Lawn Timer
The Lawn Timer implementation uses a hierarchical structure that provides efficient timer management with O(1) complexity for most operations.

## Benchmarking

The project includes comprehensive benchmarking tools to compare:
- Insertion time
- Deletion time
- Memory usage
- Scalability with different timer counts
- Performance under various load conditions

## Contributing

Feel free to submit issues and enhancement requests! 

# C benchmarks

This library provides a comprehensive benchmarking suite for comparing the performance of the Lawn and TimerWheel timer implementations. It includes benchmarks for various operations and workload patterns.

## Features

- Insertion benchmarks
- Deletion benchmarks
- Tick benchmarks
- Memory usage benchmarks
- Workload pattern benchmarks (fixed, mixed, burst, uniform)
- Stability benchmarks
- CSV output for results
- Configurable benchmark parameters

## Building

To build the benchmark library and main program, simply run:

```bash
make
```

This will create a `benchmark` executable in the current directory.

## Usage

Run the benchmark program with the following options:

```bash
./benchmark [options]
```

Options:
- `-n, --num-timers NUM`: Number of timers to use (default: 100000)
- `-r, --num-runs NUM`: Number of runs per benchmark (default: 5)
- `-o, --output FILE`: Output file for results (default: benchmark_results.csv)
- `-v, --verbose`: Enable verbose output
- `-h, --help`: Show help message

Example:
```bash
./benchmark -n 50000 -r 3 -o results.csv -v
```

## Benchmark Types

### Insertion Benchmark
Measures the time taken to insert a specified number of timers with random TTLs.

### Deletion Benchmark
Measures the time taken to delete a specified number of timers.

### Tick Benchmark
Measures the time taken to process expired timers.

### Memory Usage Benchmark
Measures the memory usage of both implementations with a specified number of timers.

### Workload Pattern Benchmark
Tests different workload patterns:
- Fixed: All timers have the same TTL (100ms)
- Mixed: Timers have TTLs from a fixed set (100ms, 500ms, 1000ms, 2000ms, 5000ms)
- Burst: Many short TTLs with some longer ones
- Uniform: TTLs uniformly distributed between 100ms and 10000ms

### Stability Benchmark
Runs a mixed workload (insertions, deletions, ticks) for a specified duration to measure stability and performance under continuous load.

## Results

Results are saved in CSV format with the following metrics:
- Number of timers
- Insertion time (ms per timer)
- Deletion time (ms per timer)
- Tick time (ms per tick)
- Memory usage (MB)

## Dependencies

- C standard library
- POSIX threads library
- Lawn timer implementation
- TimerWheel implementation

## License

This benchmark library is part of the Lawn project and is subject to the same license terms. 