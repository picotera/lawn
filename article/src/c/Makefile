CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS = -lm

# Paths
SRC_DIR = .
BUILD_DIR = ./build
BIN_DIR = ./bin

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Lawn and Timer Wheel paths
LAWN_DIR = ../../../src
WHEEL_DIR = ../../../src/utils

# Target executable
TARGET = $(BIN_DIR)/benchmark

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) $(BUILD_DIR)/lawn.o $(BUILD_DIR)/timerwheel.o $(BUILD_DIR)/hashmap.o $(BUILD_DIR)/millisecond_time.o $(BUILD_DIR)/hash_funcs.o
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build complete: $@"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(LAWN_DIR) -I$(WHEEL_DIR) -c $< -o $@

$(BUILD_DIR)/lawn.o: $(LAWN_DIR)/lawn.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(LAWN_DIR) -c $< -o $@

$(BUILD_DIR)/timerwheel.o: $(WHEEL_DIR)/timerwheel.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(WHEEL_DIR) -c $< -o $@

$(BUILD_DIR)/hashmap.o: $(LAWN_DIR)/utils/hashmap.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(LAWN_DIR) -c $< -o $@

$(BUILD_DIR)/millisecond_time.o: $(LAWN_DIR)/utils/millisecond_time.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(LAWN_DIR) -c $< -o $@

$(BUILD_DIR)/hash_funcs.o: $(LAWN_DIR)/utils/hash_funcs.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(LAWN_DIR) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Clean complete"

# Run the benchmark with default settings
help: $(TARGET)
	$(TARGET) --help

run: $(TARGET)
	$(TARGET)

# Run with verbose output
run-verbose: $(TARGET)
	$(TARGET) --verbose

# Run with custom number of timers
run-small: $(TARGET)
	$(TARGET) --num-timers 1000 --verbose

run-medium: $(TARGET)
	$(TARGET) --num-timers 10000 --verbose

run-large: $(TARGET)
	$(TARGET) --num-timers 100000 --verbose

# Run with specific workload patterns
run-uniform: $(TARGET)
	$(TARGET) --workload uniform --verbose

run-bimodal: $(TARGET)
	$(TARGET) --workload bimodal --verbose

run-exponential: $(TARGET)
	$(TARGET) --workload exponential --verbose

run-constant: $(TARGET)
	$(TARGET) --workload constant --verbose

run-realistic: $(TARGET)
	$(TARGET) --workload realistic --verbose

# Run with all workload patterns and save results to separate files
run-all-patterns: $(TARGET)
	$(TARGET) --workload uniform --output uniform_results.csv
	$(TARGET) --workload bimodal --output bimodal_results.csv
	$(TARGET) --workload exponential --output exponential_results.csv
	$(TARGET) --workload constant --output constant_results.csv
	$(TARGET) --workload realistic --output realistic_results.csv

# Run with discrete mode
run-discrete: $(TARGET)
	$(TARGET) --discrete --verbose

# Run with all workload patterns in discrete mode
run-all-discrete: $(TARGET)
	$(TARGET) --workload uniform --discrete --output uniform_discrete_results.csv
	$(TARGET) --workload bimodal --discrete --output bimodal_discrete_results.csv
	$(TARGET) --workload exponential --discrete --output exponential_discrete_results.csv
	$(TARGET) --workload constant --discrete --output constant_discrete_results.csv
	$(TARGET) --workload realistic --discrete --output realistic_discrete_results.csv

# Run with discrete mode and custom step size
run-discrete-step-500: $(TARGET)
	$(TARGET) --discrete --step 500 --verbose

run-discrete-step-2000: $(TARGET)
	$(TARGET) --discrete --step 2000 --verbose

# Add targets for running specific benchmark tests
run-insertion:
	$(TARGET) -t insertion -v

run-deletion:
	$(TARGET) -t deletion -v

run-tick:
	$(TARGET) -t tick -v

run-memory:
	$(TARGET) -t memory -v

run-workload:
	$(TARGET) -t workload -v

# Combine tests
run-core:
	$(TARGET) -t insertion,deletion,tick -v

run-all-tests:
	$(TARGET) -t all -v

# test-specific CSV outputs
csv-insertion:
	$(TARGET) -t insertion -c insertion_results.csv

csv-deletion:
	$(TARGET) -t deletion -c deletion_results.csv

csv-tick:
	$(TARGET) -t tick -c tick_results.csv

csv-memory:
	$(TARGET) -t memory -c memory_results.csv

csv-workload:
	$(TARGET) -t workload -c workload_results.csv 