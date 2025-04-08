#ifndef BENCHMARKS_H
#define BENCHMARKS_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "lawn_timerstore.h"
#include "timerwheel_timerstore.h"

// Function pointer types for timer store operations
typedef int (*timer_init_func)(void);
typedef int (*timer_cleanup_func)(void);
typedef int (*timer_start_func)(mstime_t, char*, void*);
typedef int (*timer_stop_func)(char*);
typedef int (*timer_tick_func)(void);

// Benchmark result structure
typedef struct {
    double insertion_time;
    double deletion_time;
    double tick_time;
    size_t memory_usage;
    size_t num_timers;
} BenchmarkResult;

// Benchmark configuration
typedef struct {
    size_t num_timers;
    size_t num_runs;
    bool verbose;
    const char* output_file;
} BenchmarkConfig;

// Initialize default benchmark configuration
BenchmarkConfig benchmark_config_default(void);

// Generic benchmark functions
BenchmarkResult benchmark_insertion_generic(
    BenchmarkConfig config,
    timer_init_func init_func,
    timer_cleanup_func cleanup_func,
    timer_start_func start_timer_func
);

BenchmarkResult benchmark_deletion_generic(
    BenchmarkConfig config,
    timer_init_func init_func,
    timer_cleanup_func cleanup_func,
    timer_start_func start_timer_func,
    timer_stop_func stop_timer_func
);

BenchmarkResult benchmark_tick_generic(
    BenchmarkConfig config,
    timer_init_func init_func,
    timer_cleanup_func cleanup_func,
    timer_start_func start_timer_func,
    timer_tick_func per_tick_func
);

BenchmarkResult benchmark_memory_generic(
    BenchmarkConfig config,
    timer_init_func init_func,
    timer_cleanup_func cleanup_func,
    timer_start_func start_timer_func
);

BenchmarkResult benchmark_workload_pattern_generic(
    BenchmarkConfig config,
    const char* pattern,
    timer_init_func init_func,
    timer_cleanup_func cleanup_func,
    timer_start_func start_timer_func,
    timer_tick_func per_tick_func
);

BenchmarkResult benchmark_stability_generic(
    BenchmarkConfig config,
    double duration_seconds,
    timer_init_func init_func,
    timer_cleanup_func cleanup_func,
    timer_start_func start_timer_func,
    timer_stop_func stop_timer_func,
    timer_tick_func per_tick_func
);

// Run insertion benchmark
BenchmarkResult benchmark_insertion_lawn(BenchmarkConfig config);
BenchmarkResult benchmark_insertion_timerwheel(BenchmarkConfig config);

// Run deletion benchmark
BenchmarkResult benchmark_deletion_lawn(BenchmarkConfig config);
BenchmarkResult benchmark_deletion_timerwheel(BenchmarkConfig config);

// Run tick benchmark
BenchmarkResult benchmark_tick_lawn(BenchmarkConfig config);
BenchmarkResult benchmark_tick_timerwheel(BenchmarkConfig config);

// Run memory usage benchmark
BenchmarkResult benchmark_memory_lawn(BenchmarkConfig config);
BenchmarkResult benchmark_memory_timerwheel(BenchmarkConfig config);

// Run workload pattern benchmark
BenchmarkResult benchmark_workload_pattern_lawn(BenchmarkConfig config, const char* pattern);
BenchmarkResult benchmark_workload_pattern_timerwheel(BenchmarkConfig config, const char* pattern);

// Run stability benchmark
BenchmarkResult benchmark_stability_lawn(BenchmarkConfig config, double duration_seconds);
BenchmarkResult benchmark_stability_timerwheel(BenchmarkConfig config, double duration_seconds);

// Print benchmark results
void print_benchmark_result(const char* name, BenchmarkResult result);

// Save benchmark results to CSV
void save_benchmark_results(const char* filename, BenchmarkResult lawn_result, BenchmarkResult timerwheel_result);

// Helper functions
void sleep_ms(uint64_t ms);
size_t get_memory_usage(void);

#endif // BENCHMARKS_H 