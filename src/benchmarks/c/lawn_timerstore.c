#include <stdlib.h>
#include <string.h>
#include "lawn_timerstore.h"
#include "../../utils/millisecond_time.h"
#include "../../utils/hashmap.h"
#include "../../utils/hash_funcs.h"

// Global Lawn instance
static Lawn* lawn_instance = NULL;

// Structure to store timer information
typedef struct {
    char* requestId;
    void* expiryAction;
} TimerInfo;

// Global hashmap to store timer information
static HashMap* timer_info_map = NULL;

// Hash function for the timer info map
static size_t timer_info_hash_fn(const void *k, void *ctx) {
    return string_hash((char*)k);
}

// Equal function for the timer info map
static bool timer_info_equal_fn(const void *a, const void *b, void *ctx) {
    return (strcmp((char*)a, (char*)b) == 0);
}

// Initialize the Lawn-based TimerStore adapter
int lawn_timerstore_init(void) {
    if (lawn_instance != NULL) {
        // Already initialized
        return TIMERSTORE_OK;
    }
    
    lawn_instance = newLawn();
    if (lawn_instance == NULL) {
        return TIMERSTORE_ERR;
    }
    
    // Initialize the timer info map
    timer_info_map = hashmap__new(timer_info_hash_fn, timer_info_equal_fn, NULL);
    if (timer_info_map == NULL) {
        freeLawn(lawn_instance);
        lawn_instance = NULL;
        return TIMERSTORE_ERR;
    }
    
    return TIMERSTORE_OK;
}

// Clean up resources used by the Lawn-based TimerStore adapter
int lawn_timerstore_cleanup(void) {
    if (lawn_instance == NULL) {
        return TIMERSTORE_OK;
    }
    
    // Clean up the timer info map
    if (timer_info_map != NULL) {
        // Free all TimerInfo structures
        // Use a safer approach to iterate through the hashmap
        HashMapEntry *entry, *tmp;
        int bkt;
        hashmap__for_each_entry_safe(timer_info_map, entry, tmp, bkt) {
            if (entry != NULL && entry->value != NULL) {
                TimerInfo* timerInfo = (TimerInfo*)entry->value;
                free(timerInfo->requestId);
                free(timerInfo);
            }
        }
        
        hashmap__free(timer_info_map);
        timer_info_map = NULL;
    }
    
    freeLawn(lawn_instance);
    lawn_instance = NULL;
    
    return TIMERSTORE_OK;
}


int lawn_timerstore_start_timer(mstime_t interval, char* requestId, void* expiryAction) {
    return start_timer(interval, requestId, expiryAction);
}


// Start a timer that will expire after "interval" units of time
int start_timer(mstime_t interval, char* requestId, void* expiryAction) {
    if (lawn_instance == NULL) {
        if (lawn_timerstore_init() != TIMERSTORE_OK) {
            return TIMERSTORE_ERR;
        }
    }
    
    // Create a copy of the requestId to store in the Lawn
    char* requestIdCopy = strdup(requestId);
    if (requestIdCopy == NULL) {
        return TIMERSTORE_ERR;
    }
    
    // Calculate expiration time
    mstime_t currentTime = current_time_ms();
    mstime_t expirationTime = currentTime + interval;
    
    // Add the timer to the Lawn
    int result = set_element_ttl(lawn_instance, requestIdCopy, strlen(requestIdCopy), expirationTime);
    if (result != LAWN_OK) {
        free(requestIdCopy);
        return TIMERSTORE_ERR;
    }
    
    // Store the timer info in the hashmap
    TimerInfo* timerInfo = malloc(sizeof(TimerInfo));
    if (timerInfo == NULL) {
        free(requestIdCopy);
        return TIMERSTORE_ERR;
    }
    
    timerInfo->requestId = requestIdCopy;
    timerInfo->expiryAction = expiryAction;
    
    // Add the timer info to the hashmap
    const void* oldk;
    void* oldv;
    result = hashmap__set(timer_info_map, requestIdCopy, timerInfo, &oldk, &oldv);
    if (result != 0) {
        free(timerInfo);
        free(requestIdCopy);
        return TIMERSTORE_ERR;
    }
    
    return TIMERSTORE_OK;
}


int lawn_timerstore_stop_timer(char* requestId) {
    return stop_timer(requestId);
}


// Stop a timer identified by requestId
int stop_timer(char* requestId) {
    if (lawn_instance == NULL) {
        return TIMERSTORE_ERR;
    }
    
    // Remove the timer from the Lawn
    int result = del_element_exp(lawn_instance, requestId);
    if (result != LAWN_OK) {
        return TIMERSTORE_ERR;
    }
    
    // Remove the timer info from the hashmap
    const void* oldk;
    void* oldv;
    result = hashmap__delete(timer_info_map, requestId, &oldk, &oldv);
    if (result == 0) {
        TimerInfo* timerInfo = (TimerInfo*)oldv;
        free(timerInfo->requestId);
        free(timerInfo);
    }
    
    return TIMERSTORE_OK;
}


int lawn_timerstore_per_tick_bookkeeping() {
    return per_tick_bookkeeping();
}

// Check for expired timers and process them
int per_tick_bookkeeping(void) {
    if (lawn_instance == NULL) {
        return TIMERSTORE_ERR;
    }
    
    // Get all expired elements
    ElementQueue* expiredQueue = pop_expired(lawn_instance);
    if (expiredQueue == NULL) {
        return TIMERSTORE_OK; // No expired timers
    }
    
    // Process each expired timer
    ElementQueueNode* node = expiredQueue->head;
    while (node != NULL) {
        // Call expiry_processing for each expired timer
        expiry_proccessing(node->element);
        
        // Move to the next node
        node = node->next;
    }
    
    // Free the queue (but not the nodes, as they are still in use)
    free(expiredQueue);
    
    return TIMERSTORE_OK;
}

int lawn_timerstore_expiry_proccessing(char* requestId) {
    return expiry_proccessing(requestId);
}

// Process an expired timer
int expiry_proccessing(char* requestId) {
    // Retrieve the timer info from the hashmap
    void* value;
    int result = hashmap__find(timer_info_map, requestId, &value);
    if (result != 0) {
        return TIMERSTORE_ERR;
    }
    
    TimerInfo* timerInfo = (TimerInfo*)value;
    
    // Execute the expiry action
    // In a real implementation, you would call the function pointer
    // For now, we'll just assume it's a function pointer that takes no arguments
    if (timerInfo->expiryAction != NULL) {
        ((void (*)(void))timerInfo->expiryAction)();
    }
    
    // Remove the timer info from the hashmap
    const void* oldk;
    void* oldv;
    hashmap__delete(timer_info_map, requestId, &oldk, &oldv);
    
    // Free the timer info
    free(timerInfo->requestId);
    free(timerInfo);
    
    return TIMERSTORE_OK;
} 