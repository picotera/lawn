# C Benchmarks for Timer Implementations

This directory contains C-based benchmarks for comparing the performance of different timer implementations:
- Lawn TimerStore
- TimerWheel TimerStore

## Structure

- `benchmarks.c` - Main benchmark implementation
- `benchmarks.h` - Benchmark interface definitions
- `lawn_timerstore.c` - Lawn TimerStore implementation
- `timerwheel_timerstore.c` - TimerWheel TimerStore implementation
- `timerstore.h` - Common TimerStore interface

## Building

```bash
make
```

This will build:
- `benchmark` - Main benchmark executable that runs all tests
- `lawn_timerstore_test` - Test executable for Lawn implementation
- `timerwheel_timerstore_test` - Test executable for TimerWheel implementation

## Running Benchmarks

You can run the comprehensive benchmark suite:

```bash
./benchmark
```

Or run individual test executables:

```bash
./lawn_timerstore_test
./timerwheel_timerstore_test
```

The benchmarks measure:
- Insertion performance
- Deletion performance
- Tick processing performance
- Memory usage
- Workload pattern handling
- Long-term stability

Results are output in CSV format for easy comparison. 