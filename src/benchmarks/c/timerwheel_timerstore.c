#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>  // For offsetof
#include "timerwheel_timerstore.h"
#include "../../utils/millisecond_time.h"
#include "../../utils/hashmap.h"
#include "timerwheel/timeout.h"

// Global timer wheel instance
static struct timeouts* timer_wheel = NULL;

// Structure to store timer information
typedef struct {
    char requestId[256];
    void* expiryAction;
    struct timeout timeout;
} TimerInfo;

// Global hashmap to store timer information
static struct hashmap* timer_info_map = NULL;

// Hash function for the timer info map
static size_t hash_function(const void* key, void* ctx) {
    const char* requestId = (const char*)key;
    size_t hash = 0;
    for (int i = 0; requestId[i] != '\0'; i++) {
        hash = hash * 31 + requestId[i];
    }
    return hash;
}

// Equal function for the timer info map
static bool equals_function(const void* key1, const void* key2, void* ctx) {
    return strcmp((const char*)key1, (const char*)key2) == 0;
}

// Callback function for timer expiration
static void timer_callback() {
    struct timeout* t = timeouts_get(timer_wheel);
    if (!t) {
        return;
    }
    
    TimerInfo* info = (TimerInfo*)((char*)t - offsetof(TimerInfo, timeout));
    
    // Execute the expiry action
    if (info->expiryAction) {
        ((void (*)(void))info->expiryAction)();
    }
    
    // Remove from hashmap
    hashmap__delete(timer_info_map, info->requestId, NULL, NULL);
    
    // Free the timer info
    free(info);
}

// Initialize the TimerStore using the Linux kernel timer wheel implementation
int timerwheel_timerstore_init(void) {
    if (timer_wheel != NULL) {
        // Already initialized
        return TIMERSTORE_OK;
    }
    
    // Initialize the timer wheel with 1000 Hz granularity (1ms)
    int error = 0;
    timer_wheel = timeouts_open(TIMEOUT_mHZ, &error);
    if (timer_wheel == NULL || error != 0) {
        return TIMERSTORE_ERR;
    }
    
    // Initialize the timer info map
    timer_info_map = hashmap__new(hash_function, equals_function, NULL);
    if (timer_info_map == NULL) {
        timeouts_close(timer_wheel);
        timer_wheel = NULL;
        return TIMERSTORE_ERR;
    }
    
    return TIMERSTORE_OK;
}

// Clean up resources used by the TimerStore
int timerwheel_timerstore_cleanup(void) {
    if (timer_wheel == NULL) {
        return TIMERSTORE_OK;
    }
    
    // Clean up the timer info map
    if (timer_info_map != NULL) {
        // Free all TimerInfo structures
        struct hashmap_entry *entry, *tmp;
        int bkt;
        hashmap__for_each_entry_safe(timer_info_map, entry, tmp, bkt) {
            if (entry != NULL && entry->value != NULL) {
                TimerInfo* timerInfo = (TimerInfo*)entry->value;
                
                // Remove the timer from the timer wheel
                timeouts_del(timer_wheel, &timerInfo->timeout);
                
                // Free the timer info
                free(timerInfo);
            }
        }
        
        hashmap__free(timer_info_map);
        timer_info_map = NULL;
    }
    
    // Close the timer wheel
    timeouts_close(timer_wheel);
    timer_wheel = NULL;
    
    return TIMERSTORE_OK;
}

int timerwheel_timerstore_start_timer(mstime_t interval, char* requestId, void* expiryAction) {
    return start_timer(interval, requestId, expiryAction);
}

// Start a timer that will expire after "interval" units of time
int start_timer(mstime_t interval, char* requestId, void* expiryAction) {
    if (timer_wheel == NULL) {
        if (timerwheel_timerstore_init() != TIMERSTORE_OK) {
            return TIMERSTORE_ERR;
        }
    }
    
    // Allocate timer info
    TimerInfo* info = malloc(sizeof(TimerInfo));
    if (info == NULL) {
        return TIMERSTORE_ERR;
    }
    
    // Initialize timer info
    strncpy(info->requestId, requestId, sizeof(info->requestId) - 1);
    info->requestId[sizeof(info->requestId) - 1] = '\0';
    info->expiryAction = expiryAction;
    
    // Initialize timeout
    timeout_init(&info->timeout, 0);
    struct timeout_cb cb = { timer_callback, &info->timeout };
    info->timeout.callback = cb;
    
    // Add to timer wheel
    timeouts_add(timer_wheel, &info->timeout, (timeout_t)interval);
    
    // Add to hashmap
    if (hashmap__insert(timer_info_map, info->requestId, info, HASHMAP_SET, NULL, NULL) != 0) {
        timeouts_del(timer_wheel, &info->timeout);
        free(info);
        return TIMERSTORE_ERR;
    }
    
    return TIMERSTORE_OK;
}


int timerwheel_timerstore_stop_timer(char* requestId){
    return stop_timer(requestId);
}

// Stop a timer identified by requestId
int stop_timer(char* requestId) {
    if (timer_wheel == NULL) {
        return TIMERSTORE_ERR;
    }
    
    // Find the timer info in the hashmap
    TimerInfo* info = NULL;
    hashmap__find(timer_info_map, requestId, (void**)&info);
    if (info == NULL) {
        return TIMERSTORE_ERR;
    }
    
    // Remove the timer from the timer wheel
    timeouts_del(timer_wheel, &info->timeout);
    
    // Remove the timer info from the hashmap
    hashmap__delete(timer_info_map, requestId, NULL, NULL);
    
    // Free the timer info
    free(info);
    
    return TIMERSTORE_OK;
}


int timerwheel_timerstore_per_tick_bookkeeping(){
    return per_tick_bookkeeping();
}

// Check for expired timers and process them
int per_tick_bookkeeping(void) {
    if (timer_wheel == NULL) {
        return TIMERSTORE_ERR;
    }
    
    // Update the timer wheel with the current time
    timeouts_update(timer_wheel, current_time_ms());
    
    // Process all expired timers
    struct timeout* to;
    while ((to = timeouts_get(timer_wheel)) != NULL) {
        // The callback function will handle the expiry action
        to->callback.fn();
    }
    
    return TIMERSTORE_OK;
}

int timerwheel_timerstore_expiry_proccessing(char* requestId){
    return expiry_proccessing(requestId);
}

// Process an expired timer
int expiry_proccessing(char* requestId) {
    // This function is not used in this implementation
    // The callback function handles the expiry action
    return TIMERSTORE_OK;
} 