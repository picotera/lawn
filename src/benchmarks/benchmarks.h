#ifndef BENCHMARKS_H
#define BENCHMARKS_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "../lawn.h"
#include "../timerwheel/timeout.h"

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
uint64_t get_current_time_ms(void);
void sleep_ms(uint64_t ms);
size_t get_memory_usage(void);

#endif // BENCHMARKS_H 