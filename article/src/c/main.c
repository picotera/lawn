/**
 * main.c - Benchmark entry point for comparing lawn and timerwheel datastores
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <stdbool.h>
#include "benchmarks.h"

// Include headers for the datastores
#include "../../../src/lawn.h"  // Lawn datastore
#include "../../../src/utils/timerwheel.h"  // New timer wheel datastore

// Default values
#define DEFAULT_MIN_TTL 1000
#define DEFAULT_MAX_TTL 60000
#define DEFAULT_NUM_ITERATIONS 5
#define DEFAULT_NUM_TIMERS 100000
#define DEFAULT_PATTERN WORKLOAD_UNIFORM
#define DEFAULT_VERBOSE false
#define DEFAULT_CSV_OUTPUT false
#define DEFAULT_DISCRETE_MODE false
#define DEFAULT_DISCRETE_STEP 1000
#define DEFAULT_TESTS TEST_ALL

// Timer wheel context for benchmarking
struct timerwheel_ctx {
    struct timer_wheel wheel;
    size_t num_timers;
};

/**
 * Lawn datastore adapter functions
 */
static void* lawn_init(void) {
    return (void*)newLawn();
}

static void lawn_destroy(void* ds) {
    freeLawn((Lawn*)ds);
}

static int lawn_add_timer(void* ds, const char* key, size_t key_len, uint64_t ttl) {
    return set_element_ttl((Lawn*)ds, (char*)key, key_len, ttl);
}

static int lawn_remove_timer(void* ds, const char* key) {
    return del_element_exp((Lawn*)ds, (char*)key);
}

static int lawn_tick(void* ds) {
    Lawn* lawn = (Lawn*)ds;
    ElementQueue* expired = pop_expired(lawn);
    int count = 0;
    
    if (expired) {
        count = expired->len;
        
        // Free each node in the queue and the queue itself
        ElementQueueNode* current = expired->head;
        ElementQueueNode* next = NULL;
        
        while (current != NULL) {
            next = current->next;
            free(current->element); // Free the element
            free(current);          // Free the node
            current = next;
        }
        
        // Free the queue structure
        free(expired);
    }
    
    return count;
}

static size_t lawn_size(void* ds) {
    return timer_count((Lawn*)ds);
}

/**
 * Timer wheel datastore adapter functions
 */
static void* timerwheel_init(void) {
    struct timerwheel_ctx *ctx = malloc(sizeof(*ctx));
    if (!ctx)
        return NULL;

    // Initialize timer wheel with 1ms resolution
    if (timer_wheel_init(&ctx->wheel, 1) != WHEEL_OK) {
        free(ctx);
        return NULL;
    }

    ctx->num_timers = 0;
    return ctx;
}

static void timerwheel_destroy(void* ds) {
    if (!ds)
        return;

    struct timerwheel_ctx *ctx = ds;
    timer_wheel_cleanup(&ctx->wheel);
    free(ctx);
}

static int timerwheel_add_timer(void* ds, const char* key, size_t key_len, uint64_t ttl) {
    if (!ds || !key)
        return WHEEL_ERR;

    struct timerwheel_ctx *ctx = ds;
    
    // Convert TTL from milliseconds to microseconds for timer wheel
    uint64_t expires = ctx->wheel.current_time + (ttl * 1000);

    // Add timer to wheel
    if (timer_wheel_add(&ctx->wheel, key, key_len, expires) != WHEEL_OK) {
        return WHEEL_ERR;
    }

    ctx->num_timers++;
    return WHEEL_OK;
}

static int timerwheel_remove_timer(void* ds, const char* key) {
    if (!ds || !key)
        return WHEEL_ERR;

    struct timerwheel_ctx *ctx = ds;
    int result = timer_wheel_del(&ctx->wheel, key);
    
    if (result == WHEEL_OK) {
        ctx->num_timers--;
    }
    
    return result;
}

static int timerwheel_tick(void* ds) {
    if (!ds)
        return 0;

    struct timerwheel_ctx *ctx = ds;
    uint64_t old_time = ctx->wheel.current_time;
    
    // Advance time by 1ms (in microseconds) and get expired timers
    struct expired_queue* expired = timer_wheel_advance(&ctx->wheel, old_time + 1000);
    
    int count = 0;
    if (expired) {
        count = expired->len;
        ctx->num_timers -= count;
        
        // Free the expired queue
        free_expired_queue(expired);
    }
    
    return count;
}

static size_t timerwheel_size(void* ds) {
    if (!ds)
        return 0;

    struct timerwheel_ctx *ctx = ds;
    return ctx->num_timers;
}

/**
 * Print usage information
 */
static void print_usage() {
    printf("Usage: benchmark [options]\n");
    printf("Options:\n");
    printf("  -h, --help          Print this help message\n");
    printf("  -v, --verbose       Enable verbose output\n");
    printf("  -n, --num NUM       Set number of timers to create (default: %d)\n", DEFAULT_NUM_TIMERS);
    printf("  -m, --min MS        Set minimum TTL in milliseconds (default: %d)\n", DEFAULT_MIN_TTL);
    printf("  -M, --max MS        Set maximum TTL in milliseconds (default: %d)\n", DEFAULT_MAX_TTL);
    printf("  -p, --pattern PAT   Set workload pattern (uniform, bimodal, exponential, constant, realistic) (default: uniform)\n");
    printf("  -c, --csv FILE      Write results to CSV file\n");
    printf("  -d, --discrete      Enable discrete mode (round TTLs to nearest %d ms)\n", DEFAULT_DISCRETE_STEP);
    printf("  -s, --step MS       Set discrete step size in milliseconds (requires -d) (default: %d)\n", DEFAULT_DISCRETE_STEP);
    printf("  -t, --tests TESTS   Select which tests to run (default: all)\n");
    printf("                      TESTS can be a comma-separated list of: insertion,deletion,tick,memory,workload,all\n");
    printf("                      Example: -t insertion,tick,memory\n");
}

/**
 * Parse benchmark pattern from string
 */
static workload_pattern_t parse_pattern(const char* str) {
    if (strcmp(str, "uniform") == 0) return WORKLOAD_UNIFORM;
    if (strcmp(str, "bimodal") == 0) return WORKLOAD_BIMODAL;
    if (strcmp(str, "exponential") == 0) return WORKLOAD_EXPONENTIAL;
    if (strcmp(str, "constant") == 0) return WORKLOAD_CONSTANT;
    if (strcmp(str, "realistic") == 0) return WORKLOAD_REALISTIC;
    
    fprintf(stderr, "Unknown pattern: %s\n", str);
    return DEFAULT_PATTERN;
}

/**
 * Parse comma-separated test flags
 */
static int parse_test_flags(const char* str) {
    int flags = 0;
    char* input = strdup(str);
    char* token = strtok(input, ",");
    
    while (token) {
        if (strcmp(token, "insertion") == 0) {
            flags |= TEST_INSERTION;
        } else if (strcmp(token, "deletion") == 0) {
            flags |= TEST_DELETION;
        } else if (strcmp(token, "tick") == 0) {
            flags |= TEST_TICK;
        } else if (strcmp(token, "memory") == 0) {
            flags |= TEST_MEMORY;
        } else if (strcmp(token, "workload") == 0) {
            flags |= TEST_WORKLOAD;
        } else if (strcmp(token, "all") == 0) {
            flags = TEST_ALL;
        } else {
            fprintf(stderr, "Unknown test type: %s\n", token);
        }
        token = strtok(NULL, ",");
    }
    
    free(input);
    return flags;
}

/**
 * Parse command line arguments
 */
static void parse_args(int argc, char** argv, benchmark_config_t* config) {
    // Set defaults
    config->min_ttl = DEFAULT_MIN_TTL;
    config->max_ttl = DEFAULT_MAX_TTL;
    config->num_timers = DEFAULT_NUM_TIMERS;
    config->num_iterations = DEFAULT_NUM_ITERATIONS;
    config->workload_pattern = DEFAULT_PATTERN;
    config->verbose = DEFAULT_VERBOSE;
    config->csv_output = DEFAULT_CSV_OUTPUT;
    config->discrete_mode = DEFAULT_DISCRETE_MODE;
    config->discrete_step = DEFAULT_DISCRETE_STEP;
    config->test_flags = DEFAULT_TESTS;
    config->csv_filename[0] = '\0';
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            exit(0);
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            config->verbose = true;
        } else if (strcmp(argv[i], "--num") == 0 || strcmp(argv[i], "-n") == 0) {
            if (i + 1 < argc) {
                config->num_timers = (size_t)atol(argv[++i]);
                if (config->num_timers <= 0) {
                    fprintf(stderr, "Error: Number of timers must be positive\n");
                    exit(1);
                }
            }
        } else if (strcmp(argv[i], "--min") == 0 || strcmp(argv[i], "-m") == 0) {
            if (i + 1 < argc) {
                config->min_ttl = (uint64_t)atoll(argv[++i]);
                if (config->min_ttl <= 0) {
                    fprintf(stderr, "Error: Minimum TTL must be positive\n");
                    exit(1);
                }
            }
        } else if (strcmp(argv[i], "--max") == 0 || strcmp(argv[i], "-M") == 0) {
            if (i + 1 < argc) {
                config->max_ttl = (uint64_t)atoll(argv[++i]);
                if (config->max_ttl <= 0) {
                    fprintf(stderr, "Error: Maximum TTL must be positive\n");
                    exit(1);
                }
            }
        } else if (strcmp(argv[i], "--pattern") == 0 || strcmp(argv[i], "-p") == 0) {
            if (i + 1 < argc) {
                config->workload_pattern = parse_pattern(argv[++i]);
            }
        } else if (strcmp(argv[i], "--csv") == 0 || strcmp(argv[i], "-c") == 0) {
            config->csv_output = true;
            if (i + 1 < argc && argv[i+1][0] != '-') {
                strncpy(config->csv_filename, argv[++i], sizeof(config->csv_filename)-1);
                config->csv_filename[sizeof(config->csv_filename)-1] = '\0';
            } else {
                strcpy(config->csv_filename, "benchmark_results.csv");
            }
        } else if (strcmp(argv[i], "--discrete") == 0 || strcmp(argv[i], "-d") == 0) {
            config->discrete_mode = true;
        } else if (strcmp(argv[i], "--step") == 0 || strcmp(argv[i], "-s") == 0) {
            if (i + 1 < argc) {
                config->discrete_step = (uint64_t)atoll(argv[++i]);
                if (config->discrete_step <= 0) {
                    fprintf(stderr, "Error: Discrete step must be positive\n");
                    exit(1);
                }
            }
        } else if (strcmp(argv[i], "--tests") == 0 || strcmp(argv[i], "-t") == 0) {
            if (i + 1 < argc) {
                config->test_flags = parse_test_flags(argv[++i]);
                if (config->test_flags == 0) {
                    fprintf(stderr, "Warning: No valid tests specified, using all tests\n");
                    config->test_flags = TEST_ALL;
                }
            }
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage();
            exit(1);
        }
    }
    
    if (config->min_ttl > config->max_ttl) {
        fprintf(stderr, "Error: Minimum TTL cannot be greater than maximum TTL\n");
        exit(1);
    }
}

/**
 * Print benchmark results
 */
static void print_results(benchmark_result_t* lawn_result, benchmark_result_t* timerwheel_result, benchmark_config_t* config) {
    printf("\n==== Benchmark Results ====\n\n");
    
    if (config->test_flags & TEST_INSERTION) {
        printf("Insertion Time (µs per operation):\n");
        printf("  Lawn: %.3f ± %.3f\n", lawn_result->insertion_time, lawn_result->insertion_stddev);
        printf("  Timer Wheel: %.3f ± %.3f\n", timerwheel_result->insertion_time, timerwheel_result->insertion_stddev);
        printf("  Ratio (TW/Lawn): %.2f\n\n", 
               timerwheel_result->insertion_time / lawn_result->insertion_time);
    }
    
    if (config->test_flags & TEST_DELETION) {
        printf("Deletion Time (µs per operation):\n");
        printf("  Lawn: %.3f ± %.3f\n", lawn_result->deletion_time, lawn_result->deletion_stddev);
        printf("  Timer Wheel: %.3f ± %.3f\n", timerwheel_result->deletion_time, timerwheel_result->deletion_stddev);
        printf("  Ratio (TW/Lawn): %.2f\n\n", 
               timerwheel_result->deletion_time / lawn_result->deletion_time);
    }
    
    if (config->test_flags & TEST_TICK) {
        printf("Tick Time (µs per operation):\n");
        printf("  Lawn: %.3f ± %.3f\n", lawn_result->tick_time, lawn_result->tick_stddev);
        printf("  Timer Wheel: %.3f ± %.3f\n", timerwheel_result->tick_time, timerwheel_result->tick_stddev);
        printf("  Ratio (TW/Lawn): %.2f\n\n", 
               timerwheel_result->tick_time / lawn_result->tick_time);
    }
    
    if (config->test_flags & TEST_MEMORY) {
        printf("Memory Usage (bytes):\n");
        printf("  Lawn: %zu\n", lawn_result->memory_usage);
        printf("  Timer Wheel: %zu\n", timerwheel_result->memory_usage);
        printf("  Ratio (TW/Lawn): %.2f\n\n", 
               (double)timerwheel_result->memory_usage / lawn_result->memory_usage);
    }
    
    if (config->test_flags & TEST_WORKLOAD) {
        printf("==== Workload Pattern Results ====\n\n");
        
        printf("Uniform Pattern (µs per operation):\n");
        printf("  Lawn: %.3f ± %.3f\n", lawn_result->uniform_time, lawn_result->uniform_stddev);
        printf("  Timer Wheel: %.3f ± %.3f\n", timerwheel_result->uniform_time, timerwheel_result->uniform_stddev);
        printf("  Ratio (TW/Lawn): %.2f\n\n", 
               timerwheel_result->uniform_time / lawn_result->uniform_time);
        
        printf("Bimodal Pattern (µs per operation):\n");
        printf("  Lawn: %.3f ± %.3f\n", lawn_result->bimodal_time, lawn_result->bimodal_stddev);
        printf("  Timer Wheel: %.3f ± %.3f\n", timerwheel_result->bimodal_time, timerwheel_result->bimodal_stddev);
        printf("  Ratio (TW/Lawn): %.2f\n\n", 
               timerwheel_result->bimodal_time / lawn_result->bimodal_time);
        
        printf("Exponential Pattern (µs per operation):\n");
        printf("  Lawn: %.3f ± %.3f\n", lawn_result->exponential_time, lawn_result->exponential_stddev);
        printf("  Timer Wheel: %.3f ± %.3f\n", timerwheel_result->exponential_time, timerwheel_result->exponential_stddev);
        printf("  Ratio (TW/Lawn): %.2f\n\n", 
               timerwheel_result->exponential_time / lawn_result->exponential_time);
        
        printf("Constant Pattern (µs per operation):\n");
        printf("  Lawn: %.3f ± %.3f\n", lawn_result->constant_time, lawn_result->constant_stddev);
        printf("  Timer Wheel: %.3f ± %.3f\n", timerwheel_result->constant_time, timerwheel_result->constant_stddev);
        printf("  Ratio (TW/Lawn): %.2f\n\n", 
               timerwheel_result->constant_time / lawn_result->constant_time);
        
        printf("Realistic Pattern (µs per operation):\n");
        printf("  Lawn: %.3f ± %.3f\n", lawn_result->realistic_time, lawn_result->realistic_stddev);
        printf("  Timer Wheel: %.3f ± %.3f\n", timerwheel_result->realistic_time, timerwheel_result->realistic_stddev);
        printf("  Ratio (TW/Lawn): %.2f\n\n", 
               timerwheel_result->realistic_time / lawn_result->realistic_time);
    }
}

/**
 * Main function
 */
int main(int argc, char** argv) {
    // Initialize random number generator
    srand(time(NULL));
    
    // Parse command line arguments
    benchmark_config_t config;
    parse_args(argc, argv, &config);
    
    printf("Running benchmarks with configuration:\n");
    printf("  Number of timers: %zu\n", config.num_timers);
    printf("  TTL range: %lu - %lu ms\n", config.min_ttl, config.max_ttl);
    printf("  Output file: %s\n", config.csv_filename);
    printf("  Workload pattern: %s\n", workload_pattern_to_string(config.workload_pattern));
    if (config.discrete_mode) {
        printf("  Discrete mode: enabled (step: %lu ms)\n", config.discrete_step);
    } else {
        printf("  Discrete mode: disabled\n");
    }
    
    // Print which tests will be run
    printf("  Tests to run: ");
    if (config.test_flags & TEST_INSERTION) printf("insertion ");
    if (config.test_flags & TEST_DELETION) printf("deletion ");
    if (config.test_flags & TEST_TICK) printf("tick ");
    if (config.test_flags & TEST_MEMORY) printf("memory ");
    if (config.test_flags & TEST_WORKLOAD) printf("workload ");
    printf("\n\n");
    
    // Define datastore operations for Lawn
    datastore_ops_t lawn_ops = {
        .name = "Lawn",
        .init = lawn_init,
        .destroy = lawn_destroy,
        .add_timer = lawn_add_timer,
        .remove_timer = lawn_remove_timer,
        .tick = lawn_tick,
        .size = lawn_size
    };
    
    // Define datastore operations for Timer Wheel
    datastore_ops_t timerwheel_ops = {
        .name = "Timer Wheel",
        .init = timerwheel_init,
        .destroy = timerwheel_destroy,
        .add_timer = timerwheel_add_timer,
        .remove_timer = timerwheel_remove_timer,
        .tick = timerwheel_tick,
        .size = timerwheel_size
    };
    
    // Run all benchmarks for both implementations
    benchmark_result_t lawn_result, timerwheel_result;
    printf("Running benchmarks for %s...\n", lawn_ops.name);
    run_all_benchmarks(&lawn_ops, &config, &lawn_result);
    printf("Running benchmarks for %s...\n", timerwheel_ops.name);
    run_all_benchmarks(&timerwheel_ops, &config, &timerwheel_result);
    
    // Print results
    print_results(&lawn_result, &timerwheel_result, &config);
    
    // Write results to CSV file
    if (config.csv_output) {
        write_results_to_csv(config.csv_filename, &lawn_result, &timerwheel_result, &config);
    }
    
    return 0;
} 