CC = gcc
CFLAGS = -I. -I.. -I../../timerwheel -I../../utils -I../../ -Wall -Wextra -std=c11 -O2 -pthread -D_GNU_SOURCE
LDFLAGS = -pthread -lm

SRCDIR = .
BUILD_DIR = build

# Common source files
COMMON_SRCS = benchmarks.c main.c
COMMON_OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(COMMON_SRCS:.c=.o)))

# Utility files
UTIL_SRCS = ../../utils/millisecond_time.c ../../utils/hashmap.c ../../utils/hash_funcs.c
UTIL_OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(UTIL_SRCS:.c=.o)))

# Lawn implementation
LAWN_SRCS = ../../lawn.c lawn_timerstore.c
LAWN_OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(LAWN_SRCS:.c=.o)))

# TimerWheel implementation
TIMERWHEEL_SRCS = ../../timerwheel/timeout.c timerwheel_timerstore.c
TIMERWHEEL_OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(TIMERWHEEL_SRCS:.c=.o)))

# Executables
LAWN_BENCHMARK = $(BUILD_DIR)/lawn_benchmark
TIMERWHEEL_BENCHMARK = $(BUILD_DIR)/timerwheel_benchmark

all: lawn timerwheel

lawn: $(LAWN_BENCHMARK)

timerwheel: $(TIMERWHEEL_BENCHMARK)

$(LAWN_BENCHMARK): $(COMMON_OBJS) $(UTIL_OBJS) $(LAWN_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

$(TIMERWHEEL_BENCHMARK): $(COMMON_OBJS) $(UTIL_OBJS) $(TIMERWHEEL_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRCDIR)/%.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: ../../utils/%.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: ../../%.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: ../../timerwheel/%.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean lawn timerwheel
