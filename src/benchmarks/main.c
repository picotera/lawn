#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "benchmarks.h"

void print_usage(const char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -n, --num-timers NUM    Number of timers to use (default: 100000)\n");
    printf("  -r, --num-runs NUM      Number of runs per benchmark (default: 5)\n");
    printf("  -o, --output FILE       Output file for results (default: benchmark_results.csv)\n");
    printf("  -v, --verbose           Enable verbose output\n");
    printf("  -h, --help              Show this help message\n");
}

int main(int argc, char* argv[]) {
    // Initialize random number generator
    srand(time(NULL));
    
    // Set default configuration
    BenchmarkConfig config = benchmark_config_default();
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--num-timers") == 0) {
            if (i + 1 < argc) {
                config.num_timers = (size_t)atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--num-runs") == 0) {
            if (i + 1 < argc) {
                config.num_runs = (size_t)atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                config.output_file = argv[++i];
            }
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            config.verbose = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }
    
    printf("Running benchmarks with configuration:\n");
    printf("Number of timers: %zu\n", config.num_timers);
    printf("Number of runs: %zu\n", config.num_runs);
    printf("Output file: %s\n", config.output_file);
    printf("Verbose: %s\n\n", config.verbose ? "yes" : "no");
    
    // Run insertion benchmarks
    printf("Running insertion benchmarks...\n");
    BenchmarkResult lawn_insertion = benchmark_insertion_lawn(config);
    BenchmarkResult timerwheel_insertion = benchmark_insertion_timerwheel(config);
    
    if (config.verbose) {
        print_benchmark_result("Lawn Insertion", lawn_insertion);
        print_benchmark_result("TimerWheel Insertion", timerwheel_insertion);
    }
    
    // Run deletion benchmarks
    printf("Running deletion benchmarks...\n");
    BenchmarkResult lawn_deletion = benchmark_deletion_lawn(config);
    BenchmarkResult timerwheel_deletion = benchmark_deletion_timerwheel(config);
    
    if (config.verbose) {
        print_benchmark_result("Lawn Deletion", lawn_deletion);
        print_benchmark_result("TimerWheel Deletion", timerwheel_deletion);
    }
    
    // Run tick benchmarks
    printf("Running tick benchmarks...\n");
    BenchmarkResult lawn_tick = benchmark_tick_lawn(config);
    BenchmarkResult timerwheel_tick = benchmark_tick_timerwheel(config);
    
    if (config.verbose) {
        print_benchmark_result("Lawn Tick", lawn_tick);
        print_benchmark_result("TimerWheel Tick", timerwheel_tick);
    }
    
    // Run memory usage benchmarks
    printf("Running memory usage benchmarks...\n");
    BenchmarkResult lawn_memory = benchmark_memory_lawn(config);
    BenchmarkResult timerwheel_memory = benchmark_memory_timerwheel(config);
    
    if (config.verbose) {
        print_benchmark_result("Lawn Memory", lawn_memory);
        print_benchmark_result("TimerWheel Memory", timerwheel_memory);
    }
    
    // Run workload pattern benchmarks
    printf("Running workload pattern benchmarks...\n");
    const char* patterns[] = {"fixed", "mixed", "burst", "uniform"};
    for (size_t i = 0; i < 4; i++) {
        printf("Pattern: %s\n", patterns[i]);
        BenchmarkResult lawn_pattern = benchmark_workload_pattern_lawn(config, patterns[i]);
        BenchmarkResult timerwheel_pattern = benchmark_workload_pattern_timerwheel(config, patterns[i]);
        
        if (config.verbose) {
            print_benchmark_result("Lawn Pattern", lawn_pattern);
            print_benchmark_result("TimerWheel Pattern", timerwheel_pattern);
        }
    }
    
    // Run stability benchmarks
    printf("Running stability benchmarks...\n");
    BenchmarkResult lawn_stability = benchmark_stability_lawn(config, 10.0); // 10 seconds
    BenchmarkResult timerwheel_stability = benchmark_stability_timerwheel(config, 10.0);
    
    if (config.verbose) {
        print_benchmark_result("Lawn Stability", lawn_stability);
        print_benchmark_result("TimerWheel Stability", timerwheel_stability);
    }
    
    // Save results to CSV
    printf("Saving results to %s...\n", config.output_file);
    save_benchmark_results(config.output_file, lawn_insertion, timerwheel_insertion);
    
    printf("Benchmarks completed successfully.\n");
    return 0;
} 