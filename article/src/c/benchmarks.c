/**
 * benchmarks.c - Implementation of benchmarking utilities
 */

#include "benchmarks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <math.h>

// Define CLOCK_MONOTONIC if not defined (fallback for systems without it)
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

// Define LAWN_OK and LAWN_ERR if not already defined
#ifndef LAWN_OK
#define LAWN_OK 0
#endif

#ifndef LAWN_ERR
#define LAWN_ERR 1
#endif


// Define WHEEL_OK and WHEEL_ERR if not already defined
#ifndef WHEEL_OK
#define WHEEL_OK 0
#endif

#ifndef WHEEL_ERR
#define WHEEL_ERR 1
#endif




/**
 * Sleep for the specified number of milliseconds
 */
static void sleep_ms(unsigned int ms) {
    usleep(ms * 1000);
}

/**
 * Get current time in microseconds
 */
static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

/**
 * Get current process memory usage in bytes
 */
static size_t get_memory_usage(void) {
    // First, allocate and free a substantial amount of memory
    // to force memory manager to consolidate fragmented blocks
    void* buffer = malloc(4 * 1024 * 1024); // 4MB
    if (buffer) {
        // Write to memory to ensure it's actually allocated
        memset(buffer, 1, 4 * 1024 * 1024);
        free(buffer);
    }
    
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss * 1024; // ru_maxrss returns kilobytes
}

/**
 * Calculate standard deviation from an array of values
 */
static double calculate_stddev(double* values, size_t n, double mean) {
    if (n <= 1) return 0.0;
    
    double sum_squared_diff = 0.0;
    for (size_t i = 0; i < n; i++) {
        double diff = values[i] - mean;
        sum_squared_diff += diff * diff;
    }
    
    return sqrt(sum_squared_diff / (n - 1));
}

/**
 * Generate a random TTL between min_ttl and max_ttl
 */
uint64_t generate_ttl(uint64_t min_ttl, uint64_t max_ttl) {
    return min_ttl + rand() % (max_ttl - min_ttl + 1);
}

/**
 * Generate a TTL based on the specified workload pattern
 */
uint64_t generate_ttl_with_pattern(workload_pattern_t pattern, uint64_t min_ttl, uint64_t max_ttl, 
                                  bool discrete_mode, uint64_t discrete_step) {
    uint64_t ttl = 0;
    double range = (double)(max_ttl - min_ttl);
    double r;
    
    switch (pattern) {
        case WORKLOAD_UNIFORM:
            // Simple uniform distribution (same as generate_ttl)
            ttl = min_ttl + rand() % (max_ttl - min_ttl + 1);
            break;
            
        case WORKLOAD_BIMODAL:
            // Bimodal distribution: 70% short TTLs, 30% long TTLs
            r = (double)rand() / RAND_MAX;
            if (r < 0.7) {
                // Short TTLs: first quarter of the range
                ttl = min_ttl + (uint64_t)(range * 0.25 * (double)rand() / RAND_MAX);
            } else {
                // Long TTLs: last quarter of the range
                ttl = min_ttl + (uint64_t)(range * 0.75 + range * 0.25 * (double)rand() / RAND_MAX);
            }
            break;
            
        case WORKLOAD_EXPONENTIAL:
            // Exponential distribution: more short TTLs, fewer long TTLs
            r = (double)rand() / RAND_MAX;
            // Use -log(r) to create exponential distribution
            ttl = min_ttl + (uint64_t)(range * (-log(r) / 3.0));
            if (ttl > max_ttl) ttl = max_ttl;
            break;
            
        case WORKLOAD_CONSTANT:
            // Constant TTL value (mid-range)
            ttl = min_ttl + (max_ttl - min_ttl) / 2;
            break;
            
        case WORKLOAD_REALISTIC:
            // Realistic pattern: mixture of TTLs
            // 50% short (cache), 30% medium, 20% long (session)
            r = (double)rand() / RAND_MAX;
            if (r < 0.5) {
                // Short TTLs: cache entries (1-5 seconds)
                ttl = min_ttl + (uint64_t)(range * 0.1 * (double)rand() / RAND_MAX);
            } else if (r < 0.8) {
                // Medium TTLs: application data
                ttl = min_ttl + (uint64_t)(range * 0.1 + range * 0.3 * (double)rand() / RAND_MAX);
            } else {
                // Long TTLs: session data
                ttl = min_ttl + (uint64_t)(range * 0.4 + range * 0.6 * (double)rand() / RAND_MAX);
            }
            break;
    }
    
    // Apply discrete mode if enabled
    if (discrete_mode && discrete_step > 0) {
        // Round to the nearest discrete_step
        ttl = ((ttl + (discrete_step / 2)) / discrete_step) * discrete_step;
        
        // Ensure the TTL stays within min/max bounds
        if (ttl < min_ttl) ttl = min_ttl;
        if (ttl > max_ttl) ttl = max_ttl;
    }
    
    return ttl;
}

/**
 * Generate a random key of given length
 */
char* generate_random_key(size_t length) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    char* key = malloc(length + 1);
    if (key) {
        for (size_t i = 0; i < length; i++) {
            key[i] = charset[rand() % (sizeof(charset) - 1)];
        }
        key[length] = '\0';
    }
    return key;
}

/**
 * Run insertion benchmark
 */
double benchmark_insertion(datastore_ops_t* ds_ops, benchmark_config_t* config, double* stddev) {
    if (!ds_ops || !config) return 0.0;
    
    // Initialize datastore
    void* ds = ds_ops->init();
    if (!ds) return 0.0;
    
    // Allocate array to store iteration times
    double* times = malloc(config->num_iterations * sizeof(double));
    if (!times) {
        ds_ops->destroy(ds);
        return 0.0;
    }
    
    // Run benchmark iterations
    for (size_t iter = 0; iter < config->num_iterations; iter++) {
        // Generate keys and TTLs
        char** keys = malloc(config->num_timers * sizeof(char*));
        uint64_t* ttls = malloc(config->num_timers * sizeof(uint64_t));
        
        if (!keys || !ttls) {
            if (keys) free(keys);
            if (ttls) free(ttls);
            free(times);
            ds_ops->destroy(ds);
            return 0.0;
        }
        
        // Initialize all keys to NULL
        for (size_t i = 0; i < config->num_timers; i++) {
            keys[i] = NULL;
        }
        
        // Generate keys and TTLs
        for (size_t i = 0; i < config->num_timers; i++) {
            keys[i] = generate_random_key(16); // 16-byte keys
            if (keys[i] == NULL) continue;
            
            ttls[i] = generate_ttl_with_pattern(config->workload_pattern, 
                                              config->min_ttl, 
                                              config->max_ttl, 
                                              config->discrete_mode, 
                                              config->discrete_step);
        }
        
        // Perform insertion benchmark
        uint64_t start_time = get_time_us();
        size_t successful_inserts = 0;
        
        for (size_t i = 0; i < config->num_timers; i++) {
            if (keys[i] != NULL) {
                if (ds_ops->add_timer(ds, keys[i], strlen(keys[i]), ttls[i]) == LAWN_OK) {
                    successful_inserts++;
                }
            }
        }
        
        uint64_t end_time = get_time_us();
        
        // Avoid division by zero
        successful_inserts = (successful_inserts > 0) ? successful_inserts : 1;
        times[iter] = (double)(end_time - start_time) / successful_inserts;
        
        // Clean up
        for (size_t i = 0; i < config->num_timers; i++) {
            if (keys[i] != NULL) {
                ds_ops->remove_timer(ds, keys[i]);
                free(keys[i]);
                keys[i] = NULL;
            }
        }
        
        free(keys);
        free(ttls);
    }
    
    // Calculate average
    double sum = 0.0;
    for (size_t i = 0; i < config->num_iterations; i++) {
        sum += times[i];
    }
    double avg = sum / config->num_iterations;
    
    // Calculate standard deviation if requested
    if (stddev) {
        *stddev = calculate_stddev(times, config->num_iterations, avg);
    }
    
    // Clean up
    free(times);
    ds_ops->destroy(ds);
    
    // Return average insertion time in microseconds
    return avg;
}

/**
 * Run deletion benchmark
 */
double benchmark_deletion(datastore_ops_t* ds_ops, benchmark_config_t* config, double* stddev) {
    if (!ds_ops || !config) return 0.0;
    
    // Initialize datastore
    void* ds = ds_ops->init();
    if (!ds) return 0.0;
    
    // Allocate array to store iteration times
    double* times = malloc(config->num_iterations * sizeof(double));
    if (!times) {
        ds_ops->destroy(ds);
        return 0.0;
    }
    
    // Run benchmark iterations
    for (size_t iter = 0; iter < config->num_iterations; iter++) {
        // Generate keys and TTLs
        char** keys = malloc(config->num_timers * sizeof(char*));
        if (!keys) {
            free(times);
            ds_ops->destroy(ds);
            return 0.0;
        }
        
        // Initialize all keys to NULL
        for (size_t i = 0; i < config->num_timers; i++) {
            keys[i] = NULL;
        }
        
        // Add timers to datastore
        size_t actual_timers = 0;
        for (size_t i = 0; i < config->num_timers; i++) {
            keys[i] = generate_random_key(16); // 16-byte keys
            if (keys[i] == NULL) continue;
            
            uint64_t ttl = generate_ttl_with_pattern(config->workload_pattern, 
                                                     config->min_ttl, 
                                                     config->max_ttl, 
                                                     config->discrete_mode, 
                                                     config->discrete_step);
            if (ds_ops->add_timer(ds, keys[i], strlen(keys[i]), ttl) == LAWN_OK) {
                actual_timers++;
            }
        }
        
        // Adjust if we couldn't add all timers
        size_t num_timers = (actual_timers > 0) ? actual_timers : 1;
        
        // Shuffle keys for random deletion order
        for (size_t i = 0; i < config->num_timers; i++) {
            size_t j = rand() % config->num_timers;
            char* temp = keys[i];
            keys[i] = keys[j];
            keys[j] = temp;
        }
        
        // Perform deletion benchmark
        uint64_t start_time = get_time_us();
        
        for (size_t i = 0; i < config->num_timers; i++) {
            if (keys[i] != NULL) {
                ds_ops->remove_timer(ds, keys[i]);
            }
        }
        
        uint64_t end_time = get_time_us();
        times[iter] = (double)(end_time - start_time) / num_timers;
        
        // Clean up
        for (size_t i = 0; i < config->num_timers; i++) {
            if (keys[i] != NULL) {
                free(keys[i]);
                keys[i] = NULL;
            }
        }
        
        free(keys);
    }
    
    // Calculate average
    double sum = 0.0;
    for (size_t i = 0; i < config->num_iterations; i++) {
        sum += times[i];
    }
    double avg = sum / config->num_iterations;
    
    // Calculate standard deviation if requested
    if (stddev) {
        *stddev = calculate_stddev(times, config->num_iterations, avg);
    }
    
    // Clean up
    free(times);
    ds_ops->destroy(ds);
    
    // Return average deletion time in microseconds
    return avg;
}

/**
 * Run tick benchmark
 */
double benchmark_tick(datastore_ops_t* ds_ops, benchmark_config_t* config, double* stddev) {
    if (!ds_ops || !config) return 0.0;
    
    double total_time = 0.0;
    
    // Allocate array to store tick times
    double* times = NULL;
    size_t max_ticks = config->num_timers * 2;  // Maximum possible number of ticks
    
    times = malloc(max_ticks * sizeof(double));
    if (!times) return 0.0;
    
    // Run multiple iterations to collect statistics
    size_t total_ticks = 0;
    
    for (size_t iter = 0; iter < config->num_iterations; iter++) {
        // Initialize datastore
        void* ds = ds_ops->init();
        if (!ds) {
            free(times);
            return 0.0;
        }
        
        // Track how many timers were actually added
        size_t added_timers = 0;
        
        // Add timers with short TTLs to ensure they expire during the benchmark
        for (size_t i = 0; i < config->num_timers; i++) {
            char* key = generate_random_key(16);
            if (key == NULL) continue;
            
            // Use consistent TTL ranges for both implementations
            uint64_t ttl = generate_ttl(100, 1000); // 100ms to 1s TTLs
            if (ds_ops->add_timer(ds, key, strlen(key), ttl) == LAWN_OK) {
                added_timers++;
            }
            free(key);
        }
        
        // Sleep to allow some timers to expire
        usleep(100000); // 100ms sleep
        
        // Run tick operations until all timers are processed or we reach a limit
        size_t remaining = added_timers;
        size_t tick_count = 0;
        size_t max_ticks_per_iter = (max_ticks / config->num_iterations > 0) ? 
                                   (max_ticks / config->num_iterations) : 10;
        
        while (remaining > 0 && tick_count < max_ticks_per_iter) {
            uint64_t start_time = get_time_us();
            int expired = ds_ops->tick(ds);
            uint64_t end_time = get_time_us();
            
            if (expired > 0) {
                if (total_ticks < max_ticks) {
                    times[total_ticks++] = end_time - start_time;
                }
                remaining = (expired < remaining) ? remaining - expired : 0;
            }
            
            usleep(1000); // 1ms sleep between ticks
            tick_count++;
        }
        
        // Clean up datastore
        ds_ops->destroy(ds);
    }
    
    // Calculate average
    double avg = 0.0;
    if (total_ticks > 0) {
        for (size_t i = 0; i < total_ticks; i++) {
            total_time += times[i];
        }
        avg = total_time / total_ticks;
    }
    
    // Calculate standard deviation if requested
    if (stddev && total_ticks > 0) {
        *stddev = calculate_stddev(times, total_ticks, avg);
    } else if (stddev) {
        *stddev = 0.0;
    }
    
    free(times);
    
    // Return average tick time in microseconds
    return avg;
}

/**
 * Run benchmark with specific workload pattern
 */
double benchmark_workload_pattern(datastore_ops_t* ds_ops, benchmark_config_t* config, 
                                 workload_pattern_t pattern, double* stddev) {
    if (!ds_ops || !config) return 0.0;
    
    // Save original workload pattern
    workload_pattern_t original_pattern = config->workload_pattern;
    
    // Set the requested workload pattern
    config->workload_pattern = pattern;
    
    // Run the insertion benchmark with this pattern
    double result = benchmark_insertion(ds_ops, config, stddev);
    
    // Restore original workload pattern
    config->workload_pattern = original_pattern;
    
    return result;
}

/**
 * Measure memory usage
 */
size_t benchmark_memory(datastore_ops_t* ds_ops, benchmark_config_t* config) {
    if (!ds_ops || !config) return 0;
    
    // Run benchmark multiple times to get more accurate results
    const int num_runs = config->num_iterations;
    size_t total_memory = 0;
    
    for (int run = 0; run < num_runs; run++) {
        // Force garbage collection before baseline measurement
        get_memory_usage();
        sleep_ms(50); // Allow OS to stabilize memory usage
        
        // Measure baseline memory usage
        size_t baseline = get_memory_usage();
        
        // Initialize datastore
        void* ds = ds_ops->init();
        if (!ds) continue;
        
        // Add timers
        size_t actual_timers = 0;
        for (size_t i = 0; i < config->num_timers; i++) {
            char* key = generate_random_key(16);
            if (key == NULL) continue;
            
            uint64_t ttl = generate_ttl_with_pattern(config->workload_pattern, 
                                                    config->min_ttl, 
                                                    config->max_ttl, 
                                                    config->discrete_mode, 
                                                    config->discrete_step);
            if (ds_ops->add_timer(ds, key, strlen(key), ttl) == LAWN_OK) {
                actual_timers++;
            }
            free(key);
        }
        
        // Give the memory system time to stabilize
        sleep_ms(50);
        
        // Query the datastore for its internal memory size first
        size_t reported_size = ds_ops->size(ds);
        
        // Also measure using system metrics
        size_t memory_after = get_memory_usage();
        size_t memory_diff = (memory_after > baseline) ? (memory_after - baseline) : 0;
        
        // Use the datastore's own size reporting if it's reporting non-zero memory
        // Otherwise fall back to system memory measurement
        size_t memory_usage = (reported_size > 0) ? reported_size : memory_diff;
        total_memory += memory_usage;
        
        // Clean up datastore - ensure all timers are properly removed
        if (ds_ops->destroy) {
            ds_ops->destroy(ds);
        }
        
        // Force a sleep to allow memory operations to settle
        sleep_ms(50);
    }
    
    // Return the average memory usage
    return (num_runs > 0) ? (total_memory / num_runs) : 0;
}

/**
 * Run workload pattern benchmarks
 */
void run_workload_pattern_benchmarks(datastore_ops_t* ds_ops, benchmark_config_t* config, benchmark_result_t* result) {
    if (!ds_ops || !config || !result) return;
    
    if (config->verbose) {
        printf("Running workload pattern benchmarks for %s implementation...\n", ds_ops->name);
    }
    
    // Run benchmark for each workload pattern
    if (config->verbose) printf("  Running uniform pattern benchmark...\n");
    result->uniform_time = benchmark_workload_pattern(ds_ops, config, WORKLOAD_UNIFORM, &result->uniform_stddev);
    
    if (config->verbose) printf("  Running bimodal pattern benchmark...\n");
    result->bimodal_time = benchmark_workload_pattern(ds_ops, config, WORKLOAD_BIMODAL, &result->bimodal_stddev);
    
    if (config->verbose) printf("  Running exponential pattern benchmark...\n");
    result->exponential_time = benchmark_workload_pattern(ds_ops, config, WORKLOAD_EXPONENTIAL, &result->exponential_stddev);
    
    if (config->verbose) printf("  Running constant pattern benchmark...\n");
    result->constant_time = benchmark_workload_pattern(ds_ops, config, WORKLOAD_CONSTANT, &result->constant_stddev);
    
    if (config->verbose) printf("  Running realistic pattern benchmark...\n");
    result->realistic_time = benchmark_workload_pattern(ds_ops, config, WORKLOAD_REALISTIC, &result->realistic_stddev);
    
    if (config->verbose) {
        printf("Workload pattern benchmarks for %s completed:\n", ds_ops->name);
        printf("  Uniform pattern: %.3f µs per operation (±%.3f)\n", 
               result->uniform_time, result->uniform_stddev);
        printf("  Bimodal pattern: %.3f µs per operation (±%.3f)\n", 
               result->bimodal_time, result->bimodal_stddev);
        printf("  Exponential pattern: %.3f µs per operation (±%.3f)\n", 
               result->exponential_time, result->exponential_stddev);
        printf("  Constant pattern: %.3f µs per operation (±%.3f)\n", 
               result->constant_time, result->constant_stddev);
        printf("  Realistic pattern: %.3f µs per operation (±%.3f)\n", 
               result->realistic_time, result->realistic_stddev);
    }
}

/**
 * Run all benchmarks for a given datastore implementation
 */
void run_all_benchmarks(datastore_ops_t* ds_ops, benchmark_config_t* config, benchmark_result_t* result) {
    if (!ds_ops || !config || !result) return;
    
    if (config->verbose) {
        printf("Running benchmarks for %s implementation...\n", ds_ops->name);
    }
    
    // Run insertion benchmark if selected
    if (config->test_flags & TEST_INSERTION) {
        if (config->verbose) printf("  Running insertion benchmark...\n");
        result->insertion_time = benchmark_insertion(ds_ops, config, &result->insertion_stddev);
    }
    
    // Run deletion benchmark if selected
    if (config->test_flags & TEST_DELETION) {
        if (config->verbose) printf("  Running deletion benchmark...\n");
        result->deletion_time = benchmark_deletion(ds_ops, config, &result->deletion_stddev);
    }
    
    // Run tick benchmark if selected
    if (config->test_flags & TEST_TICK) {
        if (config->verbose) printf("  Running tick benchmark...\n");
        result->tick_time = benchmark_tick(ds_ops, config, &result->tick_stddev);
    }
    
    // Measure memory usage if selected
    if (config->test_flags & TEST_MEMORY) {
        if (config->verbose) printf("  Measuring memory usage...\n");
        result->memory_usage = benchmark_memory(ds_ops, config);
    }
    
    // Run workload pattern benchmarks if selected
    if (config->test_flags & TEST_WORKLOAD) {
        run_workload_pattern_benchmarks(ds_ops, config, result);
    }
    
    if (config->verbose) {
        printf("Benchmarks for %s completed:\n", ds_ops->name);
        
        if (config->test_flags & TEST_INSERTION) {
            printf("  Insertion time: %.3f µs per operation (±%.3f)\n", 
                   result->insertion_time, result->insertion_stddev);
        }
        
        if (config->test_flags & TEST_DELETION) {
            printf("  Deletion time: %.3f µs per operation (±%.3f)\n", 
                   result->deletion_time, result->deletion_stddev);
        }
        
        if (config->test_flags & TEST_TICK) {
            printf("  Tick time: %.3f µs per operation (±%.3f)\n", 
                   result->tick_time, result->tick_stddev);
        }
        
        if (config->test_flags & TEST_MEMORY) {
            printf("  Memory usage: %zu bytes (%zu bytes per timer)\n", 
                   result->memory_usage, 
                   config->num_timers > 0 ? result->memory_usage / config->num_timers : 0);
        }
    }
}

/**
 * Get string representation of workload pattern
 */
const char* workload_pattern_to_string(workload_pattern_t pattern) {
    switch (pattern) {
        case WORKLOAD_UNIFORM:     return "Uniform";
        case WORKLOAD_BIMODAL:     return "Bimodal";
        case WORKLOAD_EXPONENTIAL: return "Exponential";
        case WORKLOAD_CONSTANT:    return "Constant";
        case WORKLOAD_REALISTIC:   return "Realistic";
        default:                   return "Unknown";
    }
}

/**
 * Write benchmark results to CSV file
 */
void write_results_to_csv(const char* filename, 
                          benchmark_result_t* lawn_result,
                          benchmark_result_t* timerwheel_result,
                          benchmark_config_t* config) {
    
    if (!filename || !lawn_result || !timerwheel_result) return;
    
    FILE* file = fopen(filename, "w");
    if (!file) {
        perror("Error opening output file");
        return;
    }
    
    // Write CSV header
    fprintf(file, "Implementation,Num Timers");
    
    if (config->test_flags & TEST_INSERTION) {
        fprintf(file, ",Insertion Time (µs),Insertion StdDev");
    }
    
    if (config->test_flags & TEST_DELETION) {
        fprintf(file, ",Deletion Time (µs),Deletion StdDev");
    }
    
    if (config->test_flags & TEST_TICK) {
        fprintf(file, ",Tick Time (µs),Tick StdDev");
    }
    
    if (config->test_flags & TEST_MEMORY) {
        fprintf(file, ",Memory Usage (bytes),Memory Per Timer (bytes)");
    }
    
    if (config->test_flags & TEST_WORKLOAD) {
        fprintf(file, ",Uniform Time (µs),Uniform StdDev");
        fprintf(file, ",Bimodal Time (µs),Bimodal StdDev");
        fprintf(file, ",Exponential Time (µs),Exponential StdDev");
        fprintf(file, ",Constant Time (µs),Constant StdDev");
        fprintf(file, ",Realistic Time (µs),Realistic StdDev");
    }
    
    fprintf(file, "\n");
    
    // Write Lawn results - start with implementation name and timer count
    fprintf(file, "Lawn,%zu", config->num_timers);
    
    // Add test results if selected
    if (config->test_flags & TEST_INSERTION) {
        fprintf(file, ",%.3f,%.3f", lawn_result->insertion_time, lawn_result->insertion_stddev);
    }
    
    if (config->test_flags & TEST_DELETION) {
        fprintf(file, ",%.3f,%.3f", lawn_result->deletion_time, lawn_result->deletion_stddev);
    }
    
    if (config->test_flags & TEST_TICK) {
        fprintf(file, ",%.3f,%.3f", lawn_result->tick_time, lawn_result->tick_stddev);
    }
    
    if (config->test_flags & TEST_MEMORY) {
        fprintf(file, ",%zu,%.3f", lawn_result->memory_usage, 
                (double)lawn_result->memory_usage / config->num_timers);
    }
    
    if (config->test_flags & TEST_WORKLOAD) {
        fprintf(file, ",%.3f,%.3f", lawn_result->uniform_time, lawn_result->uniform_stddev);
        fprintf(file, ",%.3f,%.3f", lawn_result->bimodal_time, lawn_result->bimodal_stddev);
        fprintf(file, ",%.3f,%.3f", lawn_result->exponential_time, lawn_result->exponential_stddev);
        fprintf(file, ",%.3f,%.3f", lawn_result->constant_time, lawn_result->constant_stddev);
        fprintf(file, ",%.3f,%.3f", lawn_result->realistic_time, lawn_result->realistic_stddev);
    }
    
    fprintf(file, "\n");
    
    // Write Timer Wheel results - start with implementation name and timer count
    fprintf(file, "TimerWheel,%zu", config->num_timers);
    
    // Add test results if selected
    if (config->test_flags & TEST_INSERTION) {
        fprintf(file, ",%.3f,%.3f", timerwheel_result->insertion_time, timerwheel_result->insertion_stddev);
    }
    
    if (config->test_flags & TEST_DELETION) {
        fprintf(file, ",%.3f,%.3f", timerwheel_result->deletion_time, timerwheel_result->deletion_stddev);
    }
    
    if (config->test_flags & TEST_TICK) {
        fprintf(file, ",%.3f,%.3f", timerwheel_result->tick_time, timerwheel_result->tick_stddev);
    }
    
    if (config->test_flags & TEST_MEMORY) {
        fprintf(file, ",%zu,%.3f", timerwheel_result->memory_usage, 
                (double)timerwheel_result->memory_usage / config->num_timers);
    }
    
    if (config->test_flags & TEST_WORKLOAD) {
        fprintf(file, ",%.3f,%.3f", timerwheel_result->uniform_time, timerwheel_result->uniform_stddev);
        fprintf(file, ",%.3f,%.3f", timerwheel_result->bimodal_time, timerwheel_result->bimodal_stddev);
        fprintf(file, ",%.3f,%.3f", timerwheel_result->exponential_time, timerwheel_result->exponential_stddev);
        fprintf(file, ",%.3f,%.3f", timerwheel_result->constant_time, timerwheel_result->constant_stddev);
        fprintf(file, ",%.3f,%.3f", timerwheel_result->realistic_time, timerwheel_result->realistic_stddev);
    }
    
    fprintf(file, "\n");
    
    fclose(file);
    
    printf("Results written to %s\n", filename);
} 