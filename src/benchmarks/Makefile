CC = gcc
CFLAGS = -Wall -Wextra -O2 -I.. -I../timerwheel
LDFLAGS = -lpthread

SRC_DIR = .
OBJ_DIR = obj

SRCS = benchmarks.c main.c
OBJS = $(SRCS:%.c=$(OBJ_DIR)/%.o)

TARGET = benchmark

.PHONY: all clean

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

clean:
	rm -rf $(OBJ_DIR) $(TARGET) 