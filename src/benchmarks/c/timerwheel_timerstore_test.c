#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "timerwheel_timerstore.h"
#include "../../utils/millisecond_time.h"

// Callback function for timer expiration
void timer_callback(void) {
    printf("Timer expired at %" PRIu64 " ms\n", current_time_ms());
}

int main(int argc, char* argv[]) {
    // Initialize the TimerStore using the timer wheel implementation
    if (timerwheel_timerstore_init() != TIMERSTORE_OK) {
        fprintf(stderr, "Failed to initialize TimerStore using timer wheel\n");
        return 1;
    }
    
    printf("Starting timers...\n");
    
    // Start a timer that will expire after 1 second
    if (start_timer(1000, "timer1", (void*)timer_callback) != TIMERSTORE_OK) {
        fprintf(stderr, "Failed to start timer1\n");
        timerwheel_timerstore_cleanup();
        return 1;
    }
    
    // Start another timer that will expire after 2 seconds
    if (start_timer(2000, "timer2", (void*)timer_callback) != TIMERSTORE_OK) {
        fprintf(stderr, "Failed to start timer2\n");
        timerwheel_timerstore_cleanup();
        return 1;
    }
    
    // Start a third timer that will expire after 3 seconds
    if (start_timer(3000, "timer3", (void*)timer_callback) != TIMERSTORE_OK) {
        fprintf(stderr, "Failed to start timer3\n");
        timerwheel_timerstore_cleanup();
        return 1;
    }
    
    printf("Timers started. Waiting for expiration...\n");
    
    // Wait for 4 seconds, checking for expired timers every 100ms
    for (int i = 0; i < 40; i++) {
        usleep(100000); // Sleep for 100ms
        
        // Check for expired timers
        if (per_tick_bookkeeping() != TIMERSTORE_OK) {
            fprintf(stderr, "Error in per_tick_bookkeeping\n");
        }
    }
    
    // Clean up resources
    timerwheel_timerstore_cleanup();
    
    printf("Test completed successfully\n");
    
    return 0;
} 