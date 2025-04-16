#include <stdlib.h>
#include <string.h>
#include "timerwheel.h"

// Hash function for timer entries in wheel levels
static size_t timer_hash_fn(const void *key, void *ctx)
{
    const struct timer_entry *entry = key;
    return entry->expires;
}

// Equality function for timer entries in wheel levels
static bool timer_equal_fn(const void *key1, const void *key2, void *ctx )
{
    const struct timer_entry *e1 = key1;
    const struct timer_entry *e2 = key2;
    return e1->expires == e2->expires && 
           e1->key_len == e2->key_len &&
           memcmp(e1->key, e2->key, e1->key_len) == 0;
}

// Hash function for timer map (key -> timer_entry)
static size_t key_hash_fn(const void *key, void *ctx __attribute__((unused)))
{
    const char *str = key;
    size_t hash = 5381;
    char c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

// Equality function for timer map
static bool key_equal_fn(const void *key1, const void *key2, void *ctx __attribute__((unused)))
{
    return strcmp((const char *)key1, (const char *)key2) == 0;
}

// Queue helper functions
static struct expired_node* new_expired_node(const char* key) {
    struct expired_node* node = malloc(sizeof(*node));
    if (!node)
        return NULL;

    node->key = strdup(key);
    if (!node->key) {
        free(node);
        return NULL;
    }

    node->next = NULL;
    return node;
}

static void queue_push(struct expired_queue* queue, struct expired_node* node) {
    if (!queue->head) {
        queue->head = queue->tail = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }
    queue->len++;
}

static struct expired_queue* new_expired_queue(void) {
    struct expired_queue* queue = malloc(sizeof(*queue));
    if (!queue)
        return NULL;

    queue->head = queue->tail = NULL;
    queue->len = 0;
    return queue;
}

void free_expired_queue(struct expired_queue* queue) {
    if (!queue)
        return;

    struct expired_node* current = queue->head;
    while (current) {
        struct expired_node* next = current->next;
        free(current->key);
        free(current);
        current = next;
    }

    free(queue);
}

// Initialize a wheel level
static int init_wheel_level(struct wheel_level *level, uint32_t num_slots, 
                          uint64_t slot_time, bool is_leaf) {
    level->slots = calloc(num_slots, sizeof(struct wheel_slot));
    if (!level->slots)
        return WHEEL_ERR;

    level->num_slots = num_slots;
    level->slot_time = slot_time;
    level->total_time = slot_time * num_slots;
    level->current_slot = 0;

    // Initialize slots
    for (uint32_t i = 0; i < num_slots; i++) {
        level->slots[i].is_leaf = is_leaf;
        if (is_leaf)
            level->slots[i].timers = NULL;
        else
            level->slots[i].subwheel = NULL;
    }

    return WHEEL_OK;
}

// Clean up a wheel level recursively
static void cleanup_wheel_level(struct wheel_level *level) {
    if (!level || !level->slots)
        return;

    for (uint32_t i = 0; i < level->num_slots; i++) {
        if (level->slots[i].is_leaf) {
            // Free timer list
            struct timer_entry *current = level->slots[i].timers;
            while (current) {
                struct timer_entry *next = current->next;
                free(current->key);
                free(current);
                current = next;
            }
        } else if (level->slots[i].subwheel) {
            // Recursively clean up subwheel
            cleanup_wheel_level(level->slots[i].subwheel);
            free(level->slots[i].subwheel);
        }
    }

    free(level->slots);
}

int timer_wheel_init(struct timer_wheel *tw, uint64_t resolution_ms)
{
    if (!tw || resolution_ms == 0)
        return WHEEL_ERR;

    // Initialize timer map for O(1) lookup
    tw->timer_map = hashmap__new(key_hash_fn, key_equal_fn, NULL);
    if (!tw->timer_map)
        return WHEEL_ERR;

    // Calculate number of levels needed
    tw->num_levels = 4;  // ms, seconds, minutes, hours
    tw->levels = calloc(tw->num_levels, sizeof(struct wheel_level));
    if (!tw->levels) {
        hashmap__free(tw->timer_map);
        return WHEEL_ERR;
    }

    // Initialize levels from bottom up
    if (init_wheel_level(&tw->levels[0], SLOTS_MS, resolution_ms, true) ||          // milliseconds
        init_wheel_level(&tw->levels[1], SLOTS_SEC, resolution_ms * SLOTS_MS, false) || // seconds
        init_wheel_level(&tw->levels[2], SLOTS_MIN, resolution_ms * SLOTS_MS * SLOTS_SEC, false) || // minutes
        init_wheel_level(&tw->levels[3], SLOTS_HOUR, resolution_ms * SLOTS_MS * SLOTS_SEC * SLOTS_MIN, false)) { // hours
        timer_wheel_cleanup(tw);
        return WHEEL_ERR;
    }

    tw->current_time = 0;
    tw->resolution_ms = resolution_ms;

    return WHEEL_OK;
}

void timer_wheel_cleanup(struct timer_wheel *tw)
{
    if (!tw)
        return;

    if (tw->levels) {
        for (uint32_t i = 0; i < tw->num_levels; i++) {
            cleanup_wheel_level(&tw->levels[i]);
        }
        free(tw->levels);
    }

    if (tw->timer_map)
        hashmap__free(tw->timer_map);
}

// Calculate which level and slot a timer belongs to
static void get_timer_position(struct timer_wheel *tw, uint64_t expires,
                             uint32_t *level_out, uint32_t *slot_out)
{
    uint64_t delta = expires - tw->current_time;

    for (uint32_t i = 0; i < tw->num_levels; i++) {
        if (delta < tw->levels[i].total_time) {
            *level_out = i;
            *slot_out = (expires / tw->levels[i].slot_time) % tw->levels[i].num_slots;
            return;
        }
    }

    // If we get here, put it in the highest level
    *level_out = tw->num_levels - 1;
    *slot_out = ((expires / tw->levels[tw->num_levels - 1].slot_time) % 
                 tw->levels[tw->num_levels - 1].num_slots);
}

// Helper function to recursively add a timer to a subwheel
static int recursive_add_timer(struct wheel_level *level, struct timer_entry *entry,
                             uint64_t slot_time, bool is_leaf) {
    // Calculate slot in this level
    uint32_t slot = (entry->expires / slot_time) % level->num_slots;
    
    if (is_leaf) {
        // Bottom level - add to timer list
        entry->next = level->slots[slot].timers;
        level->slots[slot].timers = entry;
        return WHEEL_OK;
    }
    
    // Higher level - ensure subwheel exists
    if (!level->slots[slot].subwheel) {
        struct wheel_level *subwheel = malloc(sizeof(struct wheel_level));
        if (!subwheel)
            return WHEEL_ERR;
            
        // Initialize subwheel with appropriate resolution
        uint64_t subwheel_slot_time = slot_time / level->num_slots;
        if (subwheel_slot_time == 0) {
            free(subwheel);
            return WHEEL_ERR;
        }
            
        if (init_wheel_level(subwheel,
                           level->num_slots,
                           subwheel_slot_time,
                           is_leaf) != WHEEL_OK) {
            free(subwheel);
            return WHEEL_ERR;
        }
        
        // Only assign to slot after successful initialization
        level->slots[slot].subwheel = subwheel;
    }
    
    // Recursively add to subwheel with adjusted slot time
    return recursive_add_timer(level->slots[slot].subwheel, entry, 
                             slot_time / level->num_slots, is_leaf);
}

// Helper function to recursively delete a timer from a subwheel
static int recursive_del_timer(struct wheel_level *level, struct timer_entry *entry,
                             uint64_t slot_time) {
    uint32_t slot = (entry->expires / slot_time) % level->num_slots;
    
    if (level->slots[slot].is_leaf) {
        // Bottom level - remove from timer list
        struct timer_entry **current = &level->slots[slot].timers;
        while (*current) {
            if (*current == entry) {
                *current = entry->next;
                return WHEEL_OK;
            }
            current = &(*current)->next;
        }
        return WHEEL_ERR;  // Not found
    }
    
    if (!level->slots[slot].subwheel)
        return WHEEL_ERR;  // Not found
        
    // Recursively delete from subwheel with adjusted slot time
    int result = recursive_del_timer(level->slots[slot].subwheel, entry, 
                                   slot_time / level->num_slots);
                                   
    // If subwheel is empty after deletion, clean it up
    if (result == WHEEL_OK) {
        bool is_empty = true;
        for (uint32_t i = 0; i < level->slots[slot].subwheel->num_slots; i++) {
            if ((level->slots[slot].subwheel->slots[i].is_leaf &&
                 level->slots[slot].subwheel->slots[i].timers != NULL) ||
                (!level->slots[slot].subwheel->slots[i].is_leaf &&
                 level->slots[slot].subwheel->slots[i].subwheel != NULL)) {
                is_empty = false;
                break;
            }
        }
        if (is_empty) {
            cleanup_wheel_level(level->slots[slot].subwheel);
            free(level->slots[slot].subwheel);
            level->slots[slot].subwheel = NULL;
        }
    }
    
    return result;
}

// Helper function to redistribute non-expired timers during cascading
static void redistribute_timers(struct timer_wheel *tw, struct timer_entry *timers,
                              uint64_t current_time) {
    struct timer_entry *current = timers;
    struct timer_entry *next;
    
    while (current) {
        next = current->next;
        if (current->expires > current_time) {
            // Calculate new position
            uint32_t level, slot;
            get_timer_position(tw, current->expires, &level, &slot);
            
            // Add to new position
            if (level == 0) {
                // Bottom level
                current->next = tw->levels[0].slots[slot].timers;
                tw->levels[0].slots[slot].timers = current;
            } else {
                // Higher level - use recursive add
                recursive_add_timer(&tw->levels[level], current,
                                 tw->levels[level].slot_time,
                                 level == 0);
            }
        }
        current = next;
    }
}

int timer_wheel_add(struct timer_wheel *tw, const char *key, size_t key_len, uint64_t expires)
{
    if (!tw || !key)
        return WHEEL_ERR;

    // First, remove any existing timer with this key
    timer_wheel_del(tw, key);

    // Create new timer entry
    struct timer_entry *entry = malloc(sizeof(*entry));
    if (!entry)
        return WHEEL_ERR;

    entry->key = malloc(key_len + 1);
    if (!entry->key) {
        free(entry);
        return WHEEL_ERR;
    }

    memcpy(entry->key, key, key_len);
    entry->key[key_len] = '\0';
    entry->key_len = key_len;
    entry->expires = expires;
    entry->next = NULL;

    // Add to timer map for O(1) lookup
    int err = hashmap__add(tw->timer_map, entry->key, entry);
    if (err) {
        free(entry->key);
        free(entry);
        return WHEEL_ERR;
    }

    // Find appropriate level and slot
    uint32_t level, slot;
    get_timer_position(tw, expires, &level, &slot);

    // Add to appropriate slot
    if (level == 0) {
        // Bottom level - add to timer list
        entry->next = tw->levels[0].slots[slot].timers;
        tw->levels[0].slots[slot].timers = entry;
    } else {
        // Higher level - use recursive add
        int result = recursive_add_timer(&tw->levels[level], entry,
                                      tw->levels[level].slot_time,
                                      level == 0);
        if (result != WHEEL_OK) {
            hashmap__delete(tw->timer_map, key, NULL, NULL);
            free(entry->key);
            free(entry);
            return WHEEL_ERR;
        }
    }

    return WHEEL_OK;
}

int timer_wheel_del(struct timer_wheel *tw, const char *key)
{
    if (!tw || !key)
        return WHEEL_ERR;

    // Find the timer entry in the map
    void *value;
    if (!hashmap__find(tw->timer_map, key, &value))
        return WHEEL_ERR;  // Key not found

    struct timer_entry *entry = value;
    
    // Find the level and slot
    uint32_t level, slot;
    get_timer_position(tw, entry->expires, &level, &slot);

    // Remove from slot
    if (level == 0) {
        // Bottom level - remove from timer list
        struct timer_entry **current = &tw->levels[0].slots[slot].timers;
        while (*current) {
            if (*current == entry) {
                *current = entry->next;
                break;
            }
            current = &(*current)->next;
        }
    } else {
        // Higher level - use recursive delete
        if (recursive_del_timer(&tw->levels[level], entry,
                              tw->levels[level].slot_time) != WHEEL_OK) {
            return WHEEL_ERR;
        }
    }

    // Remove from timer map
    hashmap__delete(tw->timer_map, key, NULL, NULL);

    // Free the entry
    free(entry->key);
    free(entry);

    return WHEEL_OK;
}

static void cascade_timers(struct wheel_level *from_level, struct wheel_level *to_level,
                         uint32_t from_slot, struct expired_queue *expired,
                         uint64_t current_time)
{
    if (!from_level || !to_level)
        return;

    struct wheel_slot *slot = &from_level->slots[from_slot];
    
    if (slot->is_leaf) {
        // Move expired timers to the expired queue
        struct timer_entry *current = slot->timers;
        struct timer_entry *next;
        
        while (current) {
            next = current->next;
            if (current->expires <= current_time) {
                struct expired_node *node = new_expired_node(current->key);
                if (node)
                    queue_push(expired, node);
            } else {
                // Redistribute non-expired timers
                struct timer_entry *non_expired = NULL;
                struct timer_entry *temp = current;
                
                // Separate expired and non-expired timers
                while (temp) {
                    next = temp->next;
                    if (temp->expires > current_time) {
                        temp->next = non_expired;
                        non_expired = temp;
                    }
                    temp = next;
                }
                
                // Redistribute non-expired timers
                if (non_expired) {
                    redistribute_timers((struct timer_wheel*)to_level, non_expired, current_time);
                }
            }
            current = next;
        }
        slot->timers = NULL;
    } else if (slot->subwheel) {
        // Cascade from subwheel
        for (uint32_t i = 0; i < slot->subwheel->num_slots; i++) {
            cascade_timers(slot->subwheel, to_level, i, expired, current_time);
        }
        // Clean up empty subwheel
        cleanup_wheel_level(slot->subwheel);
        free(slot->subwheel);
        slot->subwheel = NULL;
    }
}

struct expired_queue* timer_wheel_advance(struct timer_wheel *tw, uint64_t new_time)
{
    if (!tw || new_time <= tw->current_time)
        return NULL;

    struct expired_queue* expired = new_expired_queue();
    if (!expired)
        return NULL;

    uint64_t old_time = tw->current_time;
    tw->current_time = new_time;

    // Process each level
    for (uint32_t i = 0; i < tw->num_levels; i++) {
        uint32_t old_slot = (old_time / tw->levels[i].slot_time) % tw->levels[i].num_slots;
        uint32_t new_slot = (new_time / tw->levels[i].slot_time) % tw->levels[i].num_slots;
        
        // Process slots between old and new
        while (old_slot != new_slot) {
            if (i == 0) {
                // Bottom level - collect expired timers
                struct wheel_slot *slot = &tw->levels[0].slots[old_slot];
                struct timer_entry *current = slot->timers;
                struct timer_entry *next;
                
                while (current) {
                    next = current->next;
                    if (current->expires <= new_time) {
                        struct expired_node *node = new_expired_node(current->key);
                        if (node)
                            queue_push(expired, node);
                        hashmap__delete(tw->timer_map, current->key, NULL, NULL);
                        free(current->key);
                        free(current);
                    }
                    current = next;
                }
                slot->timers = NULL;
            } else {
                // Higher level - cascade timers down
                cascade_timers(&tw->levels[i], &tw->levels[i-1], old_slot, 
                             expired, new_time);
            }
            
            old_slot = (old_slot + 1) % tw->levels[i].num_slots;
        }
    }

    // If no timers expired, free the queue and return NULL
    if (expired->len == 0) {
        free_expired_queue(expired);
        return NULL;
    }

    return expired;
}

uint64_t timer_wheel_next_expiry(struct timer_wheel *tw)
{
    if (!tw)
        return UINT64_MAX;

    uint64_t next = UINT64_MAX;

    // Check each level
    for (uint32_t i = 0; i < tw->num_levels; i++) {
        uint32_t slot = tw->levels[i].current_slot;
        uint32_t checked = 0;
        
        while (checked < tw->levels[i].num_slots) {
            if (i == 0) {
                // Bottom level - check timer list
                struct timer_entry *timer = tw->levels[0].slots[slot].timers;
                while (timer) {
                    if (timer->expires < next)
                        next = timer->expires;
                    timer = timer->next;
                }
            } else if (tw->levels[i].slots[slot].subwheel) {
                // Check subwheel
                uint64_t subwheel_next = timer_wheel_next_expiry(
                    (struct timer_wheel*)tw->levels[i].slots[slot].subwheel);
                if (subwheel_next < next)
                    next = subwheel_next;
            }
            
            slot = (slot + 1) % tw->levels[i].num_slots;
            checked++;
        }
    }

    return next;
} 