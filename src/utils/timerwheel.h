/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */

#ifndef __TIMERWHEEL_H
#define __TIMERWHEEL_H

#include <stdint.h>
#include <stdbool.h>
#include "hashmap.h"

#define WHEEL_OK 0
#define WHEEL_ERR 1

// Default slot configurations
#define SLOTS_MS 20    // 20 slots for milliseconds (50ms resolution)
#define SLOTS_SEC 60   // 60 slots for seconds
#define SLOTS_MIN 60   // 60 slots for minutes
#define SLOTS_HOUR 24  // 24 slots for hours

// Queue structures for expired timers
struct expired_node {
    char* key;
    struct expired_node* next;
};

struct expired_queue {
    struct expired_node* head;
    struct expired_node* tail;
    size_t len;
};

// Forward declaration for slot
struct wheel_slot;

// Timer entry in the bottom level
struct timer_entry {
    char *key;              // Key for the timer
    size_t key_len;        // Length of the key
    uint64_t expires;      // Expiration time
    struct timer_entry *next;  // Next timer in slot
};

// A slot in any level can either contain timers (bottom level)
// or another wheel (higher levels)
struct wheel_slot {
    union {
        struct timer_entry *timers;     // For bottom level
        struct wheel_level *subwheel;   // For other levels
    };
    bool is_leaf;  // true if this is a bottom level slot
};

// A level in the timer wheel
struct wheel_level {
    struct wheel_slot *slots;
    uint32_t num_slots;      // Number of slots in this level
    uint64_t slot_time;      // Time duration of each slot
    uint64_t total_time;     // Total time covered by this level
    uint32_t current_slot;   // Current slot index
};

struct timer_wheel {
    struct wheel_level *levels;  // Dynamic array of levels
    uint32_t num_levels;        // Number of levels
    struct hashmap *timer_map;  // Map of key -> timer_entry for O(1) lookup
    uint64_t current_time;      // Current time in milliseconds
    uint64_t resolution_ms;     // Base time resolution in milliseconds
};

// Initialize a timer wheel
int timer_wheel_init(struct timer_wheel *tw, uint64_t resolution_ms);

// Clean up a timer wheel
void timer_wheel_cleanup(struct timer_wheel *tw);

// Add a timer to the wheel
int timer_wheel_add(struct timer_wheel *tw, const char *key, size_t key_len, uint64_t expires);

// Remove a timer from the wheel using its key
int timer_wheel_del(struct timer_wheel *tw, const char *key);

// Advance the timer wheel to the specified time
// Returns queue of expired timer keys
struct expired_queue* timer_wheel_advance(struct timer_wheel *tw, uint64_t new_time);

// Get the next expiration time
uint64_t timer_wheel_next_expiry(struct timer_wheel *tw);

// Free an expired queue
void free_expired_queue(struct expired_queue* queue);

#endif /* __TIMERWHEEL_H */ 