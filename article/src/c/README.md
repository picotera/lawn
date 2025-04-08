# Lawn vs. TimerWheel Benchmark

This benchmark compares two timer datastore implementations:
1. **Lawn** - Low Latency Timer Data-Structure for Large Scale Systems
2. **TimerWheel** - Tickless hierarchical timing wheel implementation

## Overview

The benchmark measures the following performance characteristics:
- **Insertion time**: Time to add a new timer with a TTL
- **Deletion time**: Time to remove an existing timer
- **Tick performance**: Time to process expired timers
- **Memory usage**: Memory consumption per timer
- **Workload patterns**: Performance across different TTL distribution patterns
- **Discrete TTLs**: Performance with TTLs rounded to specific intervals

## Workload Patterns

The benchmark tests performance with different TTL distributions:

1. **Uniform**: Even distribution across the TTL range
2. **Bimodal**: 70% short TTLs, 30% long TTLs
3. **Exponential**: More short TTLs, fewer long TTLs
4. **Constant**: All timers have the same TTL
5. **Realistic**: Mixed distribution modeling real-world usage (50% short cache TTLs, 30% medium application TTLs, 20% long session TTLs)

Each pattern can be run in either **continuous mode** (default) or **discrete mode**. In discrete mode, all TTLs are rounded to the nearest multiple of a configurable step size (default: 1000ms).

## Test Selection

The benchmark allows you to select which specific tests to run, allowing for more focused testing and faster benchmark iterations. The available tests are:

- **Insertion**: Measures the time to insert timers into each datastore
- **Deletion**: Measures the time to delete timers from each datastore
- **Tick**: Measures the time to tick the clock and process expired timers
- **Memory**: Measures the memory usage of each datastore
- **Workload**: Tests different workload patterns (uniform, bimodal, exponential, constant, realistic)

You can select tests using the `-t` or `--tests` option, with a comma-separated list:

```bash
# Run only insertion and memory tests
bin/benchmark --tests insertion,memory

# Run only the workload pattern tests
bin/benchmark --tests workload

# Run core operations (insertion, deletion, tick)
bin/benchmark --tests insertion,deletion,tick
```

The Makefile includes convenience targets for running specific tests:

```bash
# Run only the insertion test
make run-insertion

# Run only the memory test
make run-memory

# Run only workload pattern tests
make run-workload

# Run core operations (insertion, deletion, tick)
make run-core

# Generate CSV output for a specific test
make csv-insertion
```

Combining test selection with discrete mode is also possible:

```bash
# Run insertion tests in discrete mode
bin/benchmark --tests insertion --discrete
```

## Building the Benchmark

To build the benchmark:

```bash
make
```

## Running the Benchmark

Run with default settings (10,000 timers, 5 iterations, uniform workload, continuous mode):

```bash
make run
```

Run with verbose output:

```bash
make run-verbose
```

Run with different timer counts:

```bash
make run-small   # 1,000 timers
make run-medium  # 10,000 timers
make run-large   # 100,000 timers
```

Run with a specific workload pattern:

```bash
bin/benchmark --workload bimodal
bin/benchmark --workload exponential
bin/benchmark --workload realistic
```

Run in discrete mode (round TTLs to nearest 1000ms):

```bash
bin/benchmark --discrete
make run-discrete
```

Run all workload patterns in discrete mode:

```bash
make run-all-discrete
```

Try different discrete step sizes:

```bash
bin/benchmark --discrete --step 500  # Round to nearest 500ms
make run-discrete-step-500           # Round to nearest 500ms
make run-discrete-step-2000          # Round to nearest 2000ms
```

## Command Line Options

```
Usage: benchmark [options]
Options:
  -h, --help          Print this help message
  -v, --verbose       Enable verbose output
  -n, --num NUM       Set number of timers to create (default: 100000)
  -m, --min MS        Set minimum TTL in milliseconds (default: 1000)
  -M, --max MS        Set maximum TTL in milliseconds (default: 60000)
  -p, --pattern PAT   Set workload pattern (uniform, bimodal, exponential, constant, realistic) (default: uniform)
  -c, --csv FILE      Write results to CSV file
  -d, --discrete      Enable discrete mode (round TTLs to nearest 1000 ms)
  -s, --step MS       Set discrete step size in milliseconds (requires -d) (default: 1000)
  -t, --tests TESTS   Select which tests to run (default: all)
                      TESTS can be a comma-separated list of: insertion,deletion,tick,memory,workload,all
                      Example: -t insertion,tick,memory
```

## Output

The benchmark outputs results in both human-readable format to the console and in
CSV format to a file (default: benchmark_results.csv).

Example output:

```
==== Benchmark Results ====

Insertion Time (µs per operation):
  Lawn: 1.245 ± 0.123
  Timer Wheel: 2.631 ± 0.248
  Ratio (TW/Lawn): 2.11

Deletion Time (µs per operation):
  Lawn: 0.876 ± 0.095
  Timer Wheel: 15.429 ± 1.823
  Ratio (TW/Lawn): 17.61

Tick Time (µs per operation):
  Lawn: 0.325 ± 0.047
  Timer Wheel: 0.148 ± 0.023
  Ratio (TW/Lawn): 0.46

Memory Usage (bytes):
  Lawn: 1048576
  Timer Wheel: 2097152
  Ratio (TW/Lawn): 2.00

==== Workload Pattern Results ====

Uniform Pattern (µs per operation):
  Lawn: 1.213 ± 0.115
  Timer Wheel: 2.594 ± 0.235
  Ratio (TW/Lawn): 2.14

Bimodal Pattern (µs per operation):
  Lawn: 1.184 ± 0.109
  Timer Wheel: 3.256 ± 0.372
  Ratio (TW/Lawn): 2.75

Exponential Pattern (µs per operation):
  Lawn: 1.217 ± 0.112
  Timer Wheel: 2.824 ± 0.284
  Ratio (TW/Lawn): 2.32

Constant Pattern (µs per operation):
  Lawn: 0.983 ± 0.052
  Timer Wheel: 2.347 ± 0.128
  Ratio (TW/Lawn): 2.39

Realistic Pattern (µs per operation):
  Lawn: 1.194 ± 0.118
  Timer Wheel: 2.957 ± 0.313
  Ratio (TW/Lawn): 2.48
```

The CSV file includes additional metrics and can be used for further analysis or generating charts.

## Implementation Details

The benchmark compares two different implementations:

1. **Lawn**: A high throughput data structure optimized for handling a large set of timers with relatively small variance in TTL. It achieves O(1) for insertion and deletion of timers, and O(1) for timer expiration.

2. **TimerWheel**: A tickless hierarchical timing wheel implementation that provides efficient timer management with low overhead.

The benchmark uses a common interface to benchmark both implementations, ensuring a fair comparison.

### Discrete Mode

In discrete mode, all TTLs are rounded to the nearest multiple of a specified step size (default: 1000ms). This simulates real-world scenarios where timers are often set to "round" values like 1 second, 5 seconds, or 30 seconds, rather than arbitrary precise values. This can affect performance in some timer implementations, especially those that optimize for common TTL values.

## Dependencies

- GCC compiler
- Make
- Standard C libraries 