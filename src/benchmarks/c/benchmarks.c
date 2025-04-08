#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <pthread.h>
#include "benchmarks.h"
#include "lawn_timerstore.h"
#include "timerwheel_timerstore.h"
#include "../../utils/millisecond_time.h"
#include "timerwheel/timeout.h"

// Helper function to sleep for a specified number of milliseconds
void sleep_ms(uint64_t ms) {
    usleep(ms * 1000);
}

// Helper function to get current memory usage in bytes
size_t get_memory_usage(void) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss * 1024; // Convert from KB to bytes
}

// Initialize default benchmark configuration
BenchmarkConfig benchmark_config_default(void) {
    BenchmarkConfig config;
    config.num_timers = 100000;
    config.num_runs = 5;
    config.verbose = true;
    config.output_file = "benchmark_results.csv";
    return config;
}

// Dummy callback function for timers
static void dummy_callback(void) {
    // Do nothing
}

// Create a proper callback structure
struct timeout_cb create_dummy_callback(void) {
    struct timeout_cb cb;
    cb.fn = dummy_callback;
    cb.arg = NULL;
    return cb;
}

// Generate a random TTL between 100ms and 1000ms
uint64_t generate_ttl(void) {
    return (rand() % 91 + 10) * 10; // 100ms to 1000ms
}

// Generic benchmark function for insertion
BenchmarkResult benchmark_insertion_generic(
    BenchmarkConfig config,
    timer_init_func init_func,
    timer_cleanup_func cleanup_func,
    timer_start_func start_timer_func
) {
    BenchmarkResult result = {0};
    result.num_timers = config.num_timers;
    
    // Initialize TimerStore
    if (init_func() != TIMERSTORE_OK) {
        fprintf(stderr, "Failed to initialize TimerStore\n");
        return result;
    }
    
    // Measure initial memory usage
    size_t initial_memory = get_memory_usage();
    
    // Run benchmark
    uint64_t total_time = 0;
    char timer_id[32];
    
    for (size_t run = 0; run < config.num_runs; run++) {
        uint64_t start_time = current_time_ms();
        
        for (size_t i = 0; i < config.num_timers; i++) {
            snprintf(timer_id, sizeof(timer_id), "timer_%zu", i);
            uint64_t ttl = generate_ttl();
            struct timeout_cb cb = create_dummy_callback();
            start_timer_func(ttl, timer_id, &cb);
        }
        
        uint64_t end_time = current_time_ms();
        total_time += end_time - start_time;
        
        // Clear for next run
        cleanup_func();
        init_func();
    }
    
    // Calculate average insertion time
    result.insertion_time = (double)total_time / (config.num_runs * config.num_timers);
    
    // Measure final memory usage
    size_t final_memory = get_memory_usage();
    result.memory_usage = final_memory - initial_memory;
    
    // Clean up
    cleanup_func();
    
    return result;
}

// Run insertion benchmark for Lawn
BenchmarkResult benchmark_insertion_lawn(BenchmarkConfig config) {
    return benchmark_insertion_generic(
        config,
        lawn_timerstore_init,
        lawn_timerstore_cleanup,
        lawn_timerstore_start_timer
    );
}

// Run insertion benchmark for TimerWheel
BenchmarkResult benchmark_insertion_timerwheel(BenchmarkConfig config) {
    return benchmark_insertion_generic(
        config,
        timerwheel_timerstore_init,
        timerwheel_timerstore_cleanup,
        timerwheel_timerstore_start_timer
    );
}

// Generic benchmark function for deletion
BenchmarkResult benchmark_deletion_generic(
    BenchmarkConfig config,
    timer_init_func init_func,
    timer_cleanup_func cleanup_func,
    timer_start_func start_timer_func,
    timer_stop_func stop_timer_func
) {
    BenchmarkResult result = {0};
    result.num_timers = config.num_timers;
    
    // Initialize TimerStore
    if (init_func() != TIMERSTORE_OK) {
        fprintf(stderr, "Failed to initialize TimerStore\n");
        return result;
    }
    
    // Insert timers first
    char** timer_ids = malloc(config.num_timers * sizeof(char*));
    if (!timer_ids) {
        fprintf(stderr, "Failed to allocate memory for timer IDs\n");
        cleanup_func();
        return result;
    }
    
    for (size_t i = 0; i < config.num_timers; i++) {
        timer_ids[i] = malloc(32);
        if (!timer_ids[i]) {
            fprintf(stderr, "Failed to allocate memory for timer ID %zu\n", i);
            for (size_t j = 0; j < i; j++) {
                free(timer_ids[j]);
            }
            free(timer_ids);
            cleanup_func();
            return result;
        }
        snprintf(timer_ids[i], 32, "timer_%zu", i);
        uint64_t ttl = generate_ttl();
        struct timeout_cb cb = create_dummy_callback();
        start_timer_func(ttl, timer_ids[i], &cb);
    }
    
    // Run benchmark
    uint64_t total_time = 0;
    
    for (size_t run = 0; run < config.num_runs; run++) {
        // Reinsert timers for each run
        for (size_t i = 0; i < config.num_timers; i++) {
            uint64_t ttl = generate_ttl();
            struct timeout_cb cb = create_dummy_callback();
            start_timer_func(ttl, timer_ids[i], &cb);
        }
        
        uint64_t start_time = current_time_ms();
        
        for (size_t i = 0; i < config.num_timers; i++) {
            stop_timer_func(timer_ids[i]);
        }
        
        uint64_t end_time = current_time_ms();
        total_time += end_time - start_time;
    }
    
    // Calculate average deletion time
    result.deletion_time = (double)total_time / (config.num_runs * config.num_timers);
    
    // Clean up
    for (size_t i = 0; i < config.num_timers; i++) {
        free(timer_ids[i]);
    }
    free(timer_ids);
    cleanup_func();
    
    return result;
}

// Run deletion benchmark for Lawn
BenchmarkResult benchmark_deletion_lawn(BenchmarkConfig config) {
    return benchmark_deletion_generic(
        config,
        lawn_timerstore_init,
        lawn_timerstore_cleanup,
        lawn_timerstore_start_timer,
        lawn_timerstore_stop_timer
    );
}

// Run deletion benchmark for TimerWheel
BenchmarkResult benchmark_deletion_timerwheel(BenchmarkConfig config) {
    return benchmark_deletion_generic(
        config,
        timerwheel_timerstore_init,
        timerwheel_timerstore_cleanup,
        timerwheel_timerstore_start_timer,
        timerwheel_timerstore_stop_timer
    );
}

// Generic benchmark function for tick
BenchmarkResult benchmark_tick_generic(
    BenchmarkConfig config,
    timer_init_func init_func,
    timer_cleanup_func cleanup_func,
    timer_start_func start_timer_func,
    timer_tick_func per_tick_func
) {
    BenchmarkResult result = {0};
    result.num_timers = config.num_timers;
    
    // Initialize TimerStore
    if (init_func() != TIMERSTORE_OK) {
        fprintf(stderr, "Failed to initialize TimerStore\n");
        return result;
    }
    
    // Insert timers with short TTLs
    char timer_id[32];
    for (size_t i = 0; i < config.num_timers; i++) {
        snprintf(timer_id, sizeof(timer_id), "timer_%zu", i);
        uint64_t ttl = 100; // 100ms TTL
        struct timeout_cb cb = create_dummy_callback();
        start_timer_func(ttl, timer_id, &cb);
    }
    
    // Run benchmark
    uint64_t total_time = 0;
    size_t total_expired = 0;
    
    for (size_t run = 0; run < config.num_runs; run++) {
        // Reinsert timers for each run
        for (size_t i = 0; i < config.num_timers; i++) {
            snprintf(timer_id, sizeof(timer_id), "timer_%zu", i);
            uint64_t ttl = 100; // 100ms TTL
            struct timeout_cb cb = create_dummy_callback();
            start_timer_func(ttl, timer_id, &cb);
        }
        
        // Wait for timers to be close to expiration
        sleep_ms(90);
        
        uint64_t start_time = current_time_ms();
        
        // Process expired timers
        while (per_tick_func() == TIMERSTORE_OK) {
            total_expired++;
        }
        
        uint64_t end_time = current_time_ms();
        total_time += end_time - start_time;
    }
    
    // Calculate average tick time
    result.tick_time = (double)total_time / config.num_runs;
    
    // Clean up
    cleanup_func();
    
    return result;
}

// Run tick benchmark for Lawn
BenchmarkResult benchmark_tick_lawn(BenchmarkConfig config) {
    return benchmark_tick_generic(
        config,
        lawn_timerstore_init,
        lawn_timerstore_cleanup,
        lawn_timerstore_start_timer,
        lawn_timerstore_per_tick_bookkeeping
    );
}

// Run tick benchmark for TimerWheel
BenchmarkResult benchmark_tick_timerwheel(BenchmarkConfig config) {
    return benchmark_tick_generic(
        config,
        timerwheel_timerstore_init,
        timerwheel_timerstore_cleanup,
        timerwheel_timerstore_start_timer,
        timerwheel_timerstore_per_tick_bookkeeping
    );
}

// Generic benchmark function for memory usage
BenchmarkResult benchmark_memory_generic(
    BenchmarkConfig config,
    timer_init_func init_func,
    timer_cleanup_func cleanup_func,
    timer_start_func start_timer_func
) {
    BenchmarkResult result = {0};
    result.num_timers = config.num_timers;
    
    // Measure initial memory usage
    size_t initial_memory = get_memory_usage();
    
    // Initialize TimerStore
    if (init_func() != TIMERSTORE_OK) {
        fprintf(stderr, "Failed to initialize TimerStore\n");
        return result;
    }
    
    // Insert timers
    char timer_id[32];
    for (size_t i = 0; i < config.num_timers; i++) {
        snprintf(timer_id, sizeof(timer_id), "timer_%zu", i);
        uint64_t ttl = generate_ttl();
        struct timeout_cb cb = create_dummy_callback();
        start_timer_func(ttl, timer_id, &cb);
    }
    
    // Measure final memory usage
    size_t final_memory = get_memory_usage();
    result.memory_usage = final_memory - initial_memory;
    
    // Clean up
    cleanup_func();
    
    return result;
}

// Run memory usage benchmark for Lawn
BenchmarkResult benchmark_memory_lawn(BenchmarkConfig config) {
    return benchmark_memory_generic(
        config,
        lawn_timerstore_init,
        lawn_timerstore_cleanup,
        lawn_timerstore_start_timer
    );
}

// Run memory usage benchmark for TimerWheel
BenchmarkResult benchmark_memory_timerwheel(BenchmarkConfig config) {
    return benchmark_memory_generic(
        config,
        timerwheel_timerstore_init,
        timerwheel_timerstore_cleanup,
        timerwheel_timerstore_start_timer
    );
}

// Generic benchmark function for workload pattern
BenchmarkResult benchmark_workload_pattern_generic(
    BenchmarkConfig config,
    const char* pattern,
    timer_init_func init_func,
    timer_cleanup_func cleanup_func,
    timer_start_func start_timer_func,
    timer_tick_func per_tick_func
) {
    BenchmarkResult result = {0};
    result.num_timers = config.num_timers;
    
    // Initialize TimerStore
    if (init_func() != TIMERSTORE_OK) {
        fprintf(stderr, "Failed to initialize TimerStore\n");
        return result;
    }
    
    // Generate TTLs based on pattern
    uint64_t* ttls = malloc(config.num_timers * sizeof(uint64_t));
    if (!ttls) {
        fprintf(stderr, "Failed to allocate memory for TTLs\n");
        cleanup_func();
        return result;
    }
    
    if (strcmp(pattern, "fixed") == 0) {
        // Fixed 100ms TTL
        for (size_t i = 0; i < config.num_timers; i++) {
            ttls[i] = 100;
        }
    } else if (strcmp(pattern, "mixed") == 0) {
        // Mixed TTLs: 100ms, 500ms, 1000ms, 2000ms, 5000ms
        for (size_t i = 0; i < config.num_timers; i++) {
            int choice = rand() % 5;
            switch (choice) {
                case 0: ttls[i] = 100; break;
                case 1: ttls[i] = 500; break;
                case 2: ttls[i] = 1000; break;
                case 3: ttls[i] = 2000; break;
                case 4: ttls[i] = 5000; break;
            }
        }
    } else if (strcmp(pattern, "burst") == 0) {
        // Bursty with many short TTLs
        for (size_t i = 0; i < config.num_timers; i++) {
            int choice = rand() % 5;
            switch (choice) {
                case 0: ttls[i] = 100; break;
                case 1: ttls[i] = 100; break;
                case 2: ttls[i] = 100; break;
                case 3: ttls[i] = 1000; break;
                case 4: ttls[i] = 5000; break;
            }
        }
    } else {
        // Uniform distribution between 100ms and 10000ms
        for (size_t i = 0; i < config.num_timers; i++) {
            ttls[i] = 100 + (rand() % 9901);
        }
    }
    
    // Insert timers
    char timer_id[32];
    uint64_t start_time = current_time_ms();
    
    for (size_t i = 0; i < config.num_timers; i++) {
        snprintf(timer_id, sizeof(timer_id), "timer_%zu", i);
        struct timeout_cb cb = create_dummy_callback();
        start_timer_func(ttls[i], timer_id, &cb);
    }
    
    uint64_t end_time = current_time_ms();
    result.insertion_time = (double)(end_time - start_time) / config.num_timers;
    
    // Process expired timers
    start_time = current_time_ms();
    size_t total_expired = 0;
    
    while (per_tick_func() == TIMERSTORE_OK) {
        total_expired++;
    }
    
    end_time = current_time_ms();
    result.tick_time = (double)(end_time - start_time) / total_expired;
    
    // Clean up
    free(ttls);
    cleanup_func();
    
    return result;
}

// Run workload pattern benchmark for Lawn
BenchmarkResult benchmark_workload_pattern_lawn(BenchmarkConfig config, const char* pattern) {
    return benchmark_workload_pattern_generic(
        config,
        pattern,
        lawn_timerstore_init,
        lawn_timerstore_cleanup,
        lawn_timerstore_start_timer,
        lawn_timerstore_per_tick_bookkeeping
    );
}

// Run workload pattern benchmark for TimerWheel
BenchmarkResult benchmark_workload_pattern_timerwheel(BenchmarkConfig config, const char* pattern) {
    return benchmark_workload_pattern_generic(
        config,
        pattern,
        timerwheel_timerstore_init,
        timerwheel_timerstore_cleanup,
        timerwheel_timerstore_start_timer,
        timerwheel_timerstore_per_tick_bookkeeping
    );
}

// Generic benchmark function for stability
BenchmarkResult benchmark_stability_generic(
    BenchmarkConfig config,
    double duration_seconds,
    timer_init_func init_func,
    timer_cleanup_func cleanup_func,
    timer_start_func start_timer_func,
    timer_stop_func stop_timer_func,
    timer_tick_func per_tick_func
) {
    BenchmarkResult result = {0};
    result.num_timers = config.num_timers;
    
    // Initialize TimerStore
    if (init_func() != TIMERSTORE_OK) {
        fprintf(stderr, "Failed to initialize TimerStore\n");
        return result;
    }
    
    // Metrics
    size_t insertions = 0;
    size_t deletions = 0;
    size_t ticks = 0;
    double max_latency = 0.0;
    double total_latency = 0.0;
    
    // Run for specified duration
    uint64_t start_time = current_time_ms();
    uint64_t end_time = start_time + (uint64_t)(duration_seconds * 1000);
    
    char timer_id[32];
    size_t timer_counter = 0;
    
    while (current_time_ms() < end_time) {
        // Simulate mixed operations
        double operation = (double)rand() / RAND_MAX;
        
        if (operation < 0.4) { // 40% insertions
            uint64_t op_start = current_time_ms();
            snprintf(timer_id, sizeof(timer_id), "timer_%zu", timer_counter++);
            uint64_t ttl = generate_ttl();
            struct timeout_cb cb = create_dummy_callback();
            start_timer_func(ttl, timer_id, &cb);
            uint64_t op_end = current_time_ms();
            
            double latency = (double)(op_end - op_start) / 1000.0;
            max_latency = (latency > max_latency) ? latency : max_latency;
            total_latency += latency;
            insertions++;
            
        } else if (operation < 0.7) { // 30% deletions
            if (timer_counter > 0) {
                uint64_t op_start = current_time_ms();
                size_t idx = rand() % timer_counter;
                snprintf(timer_id, sizeof(timer_id), "timer_%zu", idx);
                stop_timer_func(timer_id);
                uint64_t op_end = current_time_ms();
                
                double latency = (double)(op_end - op_start) / 1000.0;
                max_latency = (latency > max_latency) ? latency : max_latency;
                total_latency += latency;
                deletions++;
            }
            
        } else { // 30% ticks
            uint64_t op_start = current_time_ms();
            while (per_tick_func() == TIMERSTORE_OK) {
                ticks++;
            }
            uint64_t op_end = current_time_ms();
            
            double latency = (double)(op_end - op_start) / 1000.0;
            max_latency = (latency > max_latency) ? latency : max_latency;
            total_latency += latency;
        }
    }
    
    // Calculate results
    size_t total_ops = insertions + deletions + ticks;
    result.insertion_time = total_ops > 0 ? total_latency / total_ops : 0;
    
    // Clean up
    cleanup_func();
    
    return result;
}

// Run stability benchmark for Lawn
BenchmarkResult benchmark_stability_lawn(BenchmarkConfig config, double duration_seconds) {
    return benchmark_stability_generic(
        config,
        duration_seconds,
        lawn_timerstore_init,
        lawn_timerstore_cleanup,
        lawn_timerstore_start_timer,
        lawn_timerstore_stop_timer,
        lawn_timerstore_per_tick_bookkeeping
    );
}

// Run stability benchmark for TimerWheel
BenchmarkResult benchmark_stability_timerwheel(BenchmarkConfig config, double duration_seconds) {
    return benchmark_stability_generic(
        config,
        duration_seconds,
        timerwheel_timerstore_init,
        timerwheel_timerstore_cleanup,
        timerwheel_timerstore_start_timer,
        timerwheel_timerstore_stop_timer,
        timerwheel_timerstore_per_tick_bookkeeping
    );
}

// Print benchmark results
void print_benchmark_result(const char* name, BenchmarkResult result) {
    printf("\n%s Results:\n", name);
    printf("Number of Timers: %zu\n", result.num_timers);
    printf("Insertion Time: %.6f ms per timer\n", result.insertion_time);
    printf("Deletion Time: %.6f ms per timer\n", result.deletion_time);
    printf("Tick Time: %.6f ms per tick\n", result.tick_time);
    printf("Memory Usage: %.2f MB\n", result.memory_usage / (1024.0 * 1024.0));
}

// Save benchmark results to CSV
void save_benchmark_results(const char* filename, BenchmarkResult lawn_result, BenchmarkResult timerwheel_result) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "Failed to open file %s for writing\n", filename);
        return;
    }
    
    fprintf(file, "Metric,Lawn,TimerWheel\n");
    fprintf(file, "Num Timers,%zu,%zu\n", lawn_result.num_timers, timerwheel_result.num_timers);
    fprintf(file, "Insertion Time (ms/timer),%.6f,%.6f\n", lawn_result.insertion_time, timerwheel_result.insertion_time);
    fprintf(file, "Deletion Time (ms/timer),%.6f,%.6f\n", lawn_result.deletion_time, timerwheel_result.deletion_time);
    fprintf(file, "Tick Time (ms/tick),%.6f,%.6f\n", lawn_result.tick_time, timerwheel_result.tick_time);
    fprintf(file, "Memory Usage (MB),%.2f,%.2f\n", lawn_result.memory_usage / (1024.0 * 1024.0), timerwheel_result.memory_usage / (1024.0 * 1024.0));
    
    fclose(file);
} 