#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "benchmarks.h"

// Function to print usage information
void print_usage(const char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -h, --help                 Show this help message\n");
    printf("  -i, --iterations N         Number of iterations (default: 1000)\n");
    printf("  -t, --timers N               Number of timers in the timer store (default: 1000)\n");
    printf("  -o, --output FILE       Output file for results (default: benchmark_results.csv)\n");
    printf("  -w, --workload FILE        Workload file to use\n");
    printf("  -m, --memory               Run memory usage benchmark\n");
    printf("  -p, --pattern              Run workload pattern benchmark\n");
    printf("  -st, --stability           Run stability benchmark\n");
    printf("  -in, --insertion            Run insertion benchmark\n");
    printf("  -de, --deletion            Run deletion benchmark\n");
    printf("  -ti, --tick               Run tick benchmark\n");
    printf("  -a, --all                  Run all benchmarks\n");
}

int main(int argc, char* argv[]) {
    BenchmarkConfig config = benchmark_config_default();
    int run_memory = 0;
    int run_pattern = 0;
    int run_stability = 0;
    int run_insertion = 0;
    int run_deletion = 0;
    int run_tick = 0;
    int run_all = 0;
    char* workload_file = NULL;
    char* program_name = argv[0];

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(program_name);
            return 0;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--iterations") == 0) {
            if (i + 1 < argc) {
                config.num_runs = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--timers") == 0) {
            if (i + 1 < argc) {
                config.num_timers = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                config.output_file = argv[++i];
            }
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--workload") == 0) {
            if (i + 1 < argc) {
                workload_file = argv[++i];
            }
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--memory") == 0) {
            run_memory = 1;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--pattern") == 0) {
            run_pattern = 1;
        } else if (strcmp(argv[i], "-st") == 0 || strcmp(argv[i], "--stability") == 0) {
            run_stability = 1;
        } else if (strcmp(argv[i], "-in") == 0 || strcmp(argv[i], "--insertion") == 0) {
            run_insertion = 1;
        } else if (strcmp(argv[i], "-de") == 0 || strcmp(argv[i], "--deletion") == 0) {
            run_deletion = 1;
        } else if (strcmp(argv[i], "-ti") == 0 || strcmp(argv[i], "--tick") == 0) {
            run_tick = 1;
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) {
            run_all = 1;
        }
    }

    // If no specific benchmarks are selected, run all
    if ((run_all) || (
        !run_memory && !run_pattern && !run_stability && 
        !run_insertion && !run_deletion && !run_tick)) {
        
        printf("Running all benchmarks...\n");
        run_memory = 1;
        run_pattern = 1;
        run_stability = 1;
        run_insertion = 1;
        run_deletion = 1;
        run_tick = 1;
    }

    // Run benchmarks
    if (run_insertion)    
    {
         // Run insertion benchmark
        BenchmarkResult lawn_insert_result = benchmark_insertion_lawn(config);
        printf("lawn Insertion benchmark completed.\n");
        printf("Average insertion time: %f ms\n", lawn_insert_result.insertion_time);

        BenchmarkResult wheel_insert_result = benchmark_insertion_timerwheel(config);
        printf("timerwheel Insertion benchmark completed.\n");
        printf("Average insertion time: %f ms\n", wheel_insert_result.insertion_time);
    }

    if (run_deletion) {
        // Run deletion benchmark
        BenchmarkResult lawn_delete_result = benchmark_deletion_lawn(config);
        printf("lawn Deletion benchmark completed.\n");
        printf("Average deletion time: %f ms\n", lawn_delete_result.deletion_time);

        BenchmarkResult wheel_delete_result = benchmark_deletion_timerwheel(config);
        printf("timerwheel Deletion benchmark completed.\n");
        printf("Average deletion time: %f ms\n", wheel_delete_result.deletion_time);
    }

    if (run_tick) { 
        // Run tick benchmark
        BenchmarkResult lawn_tick_result = benchmark_tick_lawn(config);
        printf("lawn Tick benchmark completed.\n");
        printf("Average tick time: %f ms\n", lawn_tick_result.tick_time);

        BenchmarkResult wheel_tick_result = benchmark_tick_timerwheel(config);
        printf("timerwheel Tick benchmark completed.\n");
        printf("Average tick time: %f ms\n", wheel_tick_result.tick_time);
    }
    if (run_memory) {
        BenchmarkResult lawn_result = benchmark_memory_lawn(config);
        printf("lawn Memory usage: %zu bytes\n", lawn_result.memory_usage);

        BenchmarkResult wheel_result = benchmark_memory_timerwheel(config);
        printf("timerwheel Memory usage: %zu bytes\n", wheel_result.memory_usage);
    }

    if (run_pattern) {
        if (workload_file) {
            BenchmarkResult lawn_result = benchmark_workload_pattern_lawn(config, workload_file);
            printf("lawn Workload pattern benchmark completed.\n");
            printf("Average insertion time: %f ms\n", lawn_result.insertion_time);
            printf("Average deletion time: %f ms\n", lawn_result.deletion_time);
            printf("Average tick time: %f ms\n", lawn_result.tick_time);

            BenchmarkResult wheel_result = benchmark_workload_pattern_timerwheel(config, workload_file);
            printf("timerwheel Workload pattern benchmark completed.\n");
            printf("Average insertion time: %f ms\n", wheel_result.insertion_time);
            printf("Average deletion time: %f ms\n", wheel_result.deletion_time);
            printf("Average tick time: %f ms\n", wheel_result.tick_time);
        } else {
            printf("Error: Workload file is required for pattern benchmark\n");
            return 1;
        }
    }

    if (run_stability) {
        BenchmarkResult lawn_result = benchmark_stability_lawn(config, 10.0);
        printf("lawn Stability benchmark completed.\n");
        printf("Memory usage: %zu bytes\n", lawn_result.memory_usage);
        printf("Average insertion time: %f ms\n", lawn_result.insertion_time);
        printf("Average deletion time: %f ms\n", lawn_result.deletion_time);
        printf("Average tick time: %f ms\n", lawn_result.tick_time);

        BenchmarkResult wheel_result = benchmark_stability_timerwheel(config, 10.0);
        printf("timerwheel Stability benchmark completed.\n");
        printf("Memory usage: %zu bytes\n", wheel_result.memory_usage);
        printf("Average insertion time: %f ms\n", wheel_result.insertion_time);
        printf("Average deletion time: %f ms\n", wheel_result.deletion_time);
    }

    return 0;
} 