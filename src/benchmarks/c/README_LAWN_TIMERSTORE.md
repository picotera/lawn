# Lawn-based TimerStore Adapter

This adapter implements the TimerStore interface using the Lawn data structure. The Lawn data structure is a high-throughput timer data store that can efficiently handle a large number of timers with relatively small variance in TTL (Time To Live).

## Overview

The Lawn-based TimerStore adapter provides the following functionality:

1. `start_timer`: Start a timer that will expire after a specified interval
2. `stop_timer`: Stop a timer identified by a request ID
3. `per_tick_bookkeeping`: Check for expired timers and process them
4. `expiry_proccessing`: Process an expired timer

## Usage

### Initialization and Cleanup

Before using the Lawn-based TimerStore adapter, you need to initialize it:

```c
#include "lawn_timerstore.h"

// Initialize the adapter
if (lawn_timerstore_init() != TIMERSTORE_OK) {
    // Handle error
}

// Use the adapter...

// Clean up resources when done
lawn_timerstore_cleanup();
```

### Starting a Timer

To start a timer, use the `start_timer` function:

```c
#include "timerstore.h"

// Define a callback function for timer expiration
void my_timer_callback(void) {
    // Handle timer expiration
}

// Start a timer that will expire after 1000 milliseconds (1 second)
if (start_timer(1000, "my_timer_id", (void*)my_timer_callback) != TIMERSTORE_OK) {
    // Handle error
}
```

### Stopping a Timer

To stop a timer, use the `stop_timer` function:

```c
#include "timerstore.h"

// Stop a timer identified by "my_timer_id"
if (stop_timer("my_timer_id") != TIMERSTORE_OK) {
    // Handle error
}
```

### Checking for Expired Timers

To check for expired timers and process them, use the `per_tick_bookkeeping` function:

```c
#include "timerstore.h"

// Check for expired timers and process them
if (per_tick_bookkeeping() != TIMERSTORE_OK) {
    // Handle error
}
```

## Building and Testing

To build the Lawn-based TimerStore adapter and its test program, run:

```bash
make
```

To run the test program:

```bash
./lawn_timerstore_test
```

## Implementation Details

The Lawn-based TimerStore adapter uses the following components:

1. **Lawn Data Structure**: A high-throughput timer data store that efficiently handles a large number of timers.
2. **Hashmap**: Used to store timer information, including the request ID and expiry action.
3. **Millisecond Time Utility**: Used to get the current time in milliseconds.

The adapter implements the TimerStore interface by:

1. Using the Lawn data structure to store and manage timers.
2. Using a hashmap to store timer information, including the request ID and expiry action.
3. Implementing the TimerStore interface functions to interact with the Lawn data structure.

## Performance Characteristics

The Lawn data structure provides the following performance characteristics:

- O(1) for insertion and deletion of timers
- O(1) for timer expiration
- Efficient handling of a large number of timers with relatively small variance in TTL

These characteristics make the Lawn-based TimerStore adapter suitable for high-throughput applications that need to manage a large number of timers. 