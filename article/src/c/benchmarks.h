/**
 * benchmarks.h - Benchmarking utilities for comparing datastore implementations
 * 
 * This file contains the declarations for functions used to benchmark
 * different timer datastore implementations (lawn and timerwheel).
 */

#ifndef BENCHMARKS_H
#define BENCHMARKS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/**
 * Workload pattern types for benchmark tests
 */
typedef enum {
    WORKLOAD_UNIFORM,     // Uniform distribution of TTLs
    WORKLOAD_BIMODAL,     // Bimodal distribution (short and long TTLs)
    WORKLOAD_EXPONENTIAL, // Exponential distribution (more short TTLs)
    WORKLOAD_CONSTANT,    // Constant TTL values
    WORKLOAD_REALISTIC    // Realistic distribution modeling real-world usage
} workload_pattern_t;

/**
 * Function pointer types for the operations we'll benchmark
 */
typedef void* (*init_func_t)(void);
typedef void (*destroy_func_t)(void*);
typedef int (*add_timer_func_t)(void*, const char*, size_t, uint64_t);
typedef int (*remove_timer_func_t)(void*, const char*);
typedef int (*tick_func_t)(void*);
typedef size_t (*size_func_t)(void*);

/**
 * Datastore operations structure - holds function pointers for a specific implementation
 */
typedef struct {
    const char* name;
    init_func_t init;
    destroy_func_t destroy;
    add_timer_func_t add_timer;
    remove_timer_func_t remove_timer;
    tick_func_t tick;
    size_func_t size;
} datastore_ops_t;

/**
 * Benchmark test types
 */
typedef enum {
    TEST_INSERTION = 0x01,
    TEST_DELETION  = 0x02,
    TEST_TICK      = 0x04,
    TEST_MEMORY    = 0x08,
    TEST_WORKLOAD  = 0x10,
    TEST_ALL       = 0x1F
} benchmark_test_t;

/**
 * Benchmark configuration structure
 */
typedef struct {
    size_t num_timers;       // Number of timers to add in insertion benchmarks
    size_t num_iterations;   // Number of iterations to run for each benchmark
    bool verbose;            // Print detailed information
    uint64_t min_ttl;        // Minimum TTL in milliseconds
    uint64_t max_ttl;        // Maximum TTL in milliseconds 
    bool csv_output;         // Whether to output results to CSV
    char csv_filename[256];  // File to write results to (CSV format)
    workload_pattern_t workload_pattern; // Workload pattern to use
    bool discrete_mode;      // Whether to round TTLs to the nearest step
    uint64_t discrete_step;  // Step size for discrete mode (in ms)
    int test_flags;          // Flags indicating which tests to run
} benchmark_config_t;

/**
 * Benchmark result structure
 */
typedef struct {
    double insertion_time;   // Average time per insertion (microseconds)
    double insertion_stddev; // Standard deviation of insertion time
    double deletion_time;    // Average time per deletion (microseconds)
    double deletion_stddev;  // Standard deviation of deletion time
    double tick_time;        // Average time per tick operation (microseconds)
    double tick_stddev;      // Standard deviation of tick time
    size_t memory_usage;     // Memory usage in bytes
    // Workload pattern results
    double uniform_time;     // Average time for uniform pattern
    double uniform_stddev;   // Standard deviation for uniform pattern
    double bimodal_time;     // Average time for bimodal pattern
    double bimodal_stddev;   // Standard deviation for bimodal pattern
    double exponential_time; // Average time for exponential pattern
    double exponential_stddev; // Standard deviation for exponential pattern
    double constant_time;    // Average time for constant pattern
    double constant_stddev;  // Standard deviation for constant pattern
    double realistic_time;   // Average time for realistic pattern
    double realistic_stddev; // Standard deviation for realistic pattern
} benchmark_result_t;

/**
 * Generate a random TTL between min_ttl and max_ttl
 */
uint64_t generate_ttl(uint64_t min_ttl, uint64_t max_ttl);

/**
 * Generate a TTL based on the specified workload pattern
 */
uint64_t generate_ttl_with_pattern(workload_pattern_t pattern, uint64_t min_ttl, uint64_t max_ttl,
                                  bool discrete_mode, uint64_t discrete_step);

/**
 * Generate a random key of given length
 */
char* generate_random_key(size_t length);

/**
 * Run insertion benchmark
 * 
 * @param ds_ops Datastore operations structure
 * @param config Benchmark configuration
 * @param stddev Pointer to store standard deviation (can be NULL)
 * @return Average time per insertion in microseconds
 */
double benchmark_insertion(datastore_ops_t* ds_ops, benchmark_config_t* config, double* stddev);

/**
 * Run deletion benchmark
 * 
 * @param ds_ops Datastore operations structure
 * @param config Benchmark configuration
 * @param stddev Pointer to store standard deviation (can be NULL)
 * @return Average time per deletion in microseconds
 */
double benchmark_deletion(datastore_ops_t* ds_ops, benchmark_config_t* config, double* stddev);

/**
 * Run tick benchmark
 * 
 * @param ds_ops Datastore operations structure
 * @param config Benchmark configuration
 * @param stddev Pointer to store standard deviation (can be NULL)
 * @return Average time per tick in microseconds
 */
double benchmark_tick(datastore_ops_t* ds_ops, benchmark_config_t* config, double* stddev);

/**
 * Run benchmark with specific workload pattern
 * 
 * @param ds_ops Datastore operations structure
 * @param config Benchmark configuration
 * @param pattern Workload pattern to use
 * @param stddev Pointer to store standard deviation (can be NULL)
 * @return Average time per operation in microseconds
 */
double benchmark_workload_pattern(datastore_ops_t* ds_ops, benchmark_config_t* config, 
                                 workload_pattern_t pattern, double* stddev);

/**
 * Measure memory usage
 * 
 * @param ds_ops Datastore operations structure
 * @param config Benchmark configuration
 * @return Memory usage in bytes
 */
size_t benchmark_memory(datastore_ops_t* ds_ops, benchmark_config_t* config);

/**
 * Run all benchmarks for a given datastore implementation
 * 
 * @param ds_ops Datastore operations structure
 * @param config Benchmark configuration
 * @param result Structure to store results
 */
void run_all_benchmarks(datastore_ops_t* ds_ops, benchmark_config_t* config, benchmark_result_t* result);

/**
 * Run workload pattern benchmarks
 * 
 * @param ds_ops Datastore operations structure
 * @param config Benchmark configuration
 * @param result Structure to store results
 */
void run_workload_pattern_benchmarks(datastore_ops_t* ds_ops, benchmark_config_t* config, benchmark_result_t* result);

/**
 * Write benchmark results to CSV file
 * 
 * @param filename Output filename
 * @param lawn_result Results for lawn implementation
 * @param timerwheel_result Results for timerwheel implementation
 * @param config Benchmark configuration
 */
void write_results_to_csv(const char* filename, 
                          benchmark_result_t* lawn_result,
                          benchmark_result_t* timerwheel_result,
                          benchmark_config_t* config);

/**
 * Get string representation of workload pattern
 */
const char* workload_pattern_to_string(workload_pattern_t pattern);

#endif /* BENCHMARKS_H */ 