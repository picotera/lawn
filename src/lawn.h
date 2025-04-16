/* ###################################################################
 *  Lawn - Low Latancy Timer Data-Structure for Large Scale Systems 
 *  
 *  Autor: Adam Lev-Libfeld (adam@tamarlabs.com) 2017-2025
 *
 *  TL;DR - A Lawn is a timer data store, not unlike Timer-Wheel, but with 
 *  unlimited timer span with no degrigation in performance over a large set of timers.
 *
 *  Lawn is a high troughput data structure that is based on the assumption 
 *  that most timers are set to a small set of TTLs to boost overall DS 
 *  performance. It can assist when handling a large set of timers with 
 *  relativly small variance in TTL by effectivly using minimal queues 
 *  to store timer data. Achieving O(1) for insertion and deletion of timers, 
 *  and O(1) for tiemr expiration.
 *
 *  Lawn is distributed Under the Apache2 licence 
 *
 * ################################################################### */
 
 
#ifndef LAWN_LIB_H
#define LAWN_LIB_H

#include "utils/millisecond_time.h"
#include "utils/hashmap.h" // libbpf hashmap (Linux impl. by Facebook)
#include <sys/queue.h> // TAILQ(3)

#define LAWN_OK 0
#define LAWN_ERR 1

#define LAWN_LATANCY_PADDING_MS 0 // elements will be poped prematurly at most this time


/***************************
 *  Linked Queue Definitions
 ***************************/

typedef struct element_queue_node{
    char* element;
    size_t element_len;
    mstime_t ttl_queue;
    mstime_t expiration;
    struct element_queue_node* next;
    struct element_queue_node* prev;
} ElementQueueNode;

typedef struct element_queue{
    ElementQueueNode* head;
    ElementQueueNode* tail;
    size_t len;
} ElementQueue;

/***************************
 *    Lawn Definition
 ***************************/

typedef struct hashmap_entry HashMapEntry;
typedef struct hashmap HashMap;

typedef struct lawn{
    HashMap * timeout_queues; //<ttl_queue,ElementQueue>
    HashMap * element_nodes; //<element_id,node*>
    mstime_t next_expiration;
} Lawn;


/***************************
 * CONSTRUCTOR/ DESTRUCTOR
 ***************************/
Lawn* newLawn(void);

void freeLawn(Lawn* dehy);


/***************************
 *    Function Alias
 ***************************/

#define lawnAdd set_element_ttl
#define lawnDel del_element_exp
#define lawnPop pop_expired

/************************************
 *   General DS handling functions
 ************************************/

/*
 * @return the number of unique ttl queues in the lawn
 */
size_t ttl_count(Lawn* lawn);

/*
 * @return the number of unique timers in the lawn
 */
size_t timer_count(Lawn* lawn);

/*
 * Insert ttl for a new key or update an existing one
 * @return LAWN_OK on success, LAWN_ERR on error
 */
int set_element_ttl(Lawn* lawn, char* key, size_t len, mstime_t ttl_ms);

/*
 * Alias for set_element_ttl
 * @return LAWN_OK on success, LAWN_ERR on error
 */
int add_new_node(Lawn* lawn, char* element_key, size_t len, mstime_t ttl_ms);

/*
 * Get the expiration value for the given key
 * @return datetime of expiration (in milliseconds) on success, -1 on error
 */
mstime_t get_element_exp(Lawn* lawn, char* key);

/*
 * Remove TTL from the lawn for the given key
 * @return LAWN_OK
 */
int del_element_exp(Lawn* lawn, char* key);

/*
 * @return the closest element expiration datetime (in milliseconds), or -1 if DS is empty
 */
mstime_t next_at(Lawn* lawn);

/*
 * Remove the element with the closest expiration datetime from the lawn and return it
 * @return a pointer to the node containing the element with closest 
 * expiration datetime or NULL if the lawn is empty.
 */
ElementQueueNode* pop_next(Lawn* lawn);

/*
 * @return a queue of all exired element nodes.
 */
ElementQueue* pop_expired(Lawn* lawn);



/**********************
 *  
 *      QUEUE UTILS
 * 
 **********************/

void freeQueue(ElementQueue* queue);

void queuePush(ElementQueue* queue, ElementQueueNode* node);

ElementQueueNode* queuePop(ElementQueue* queue);



/***************************
 *  
 *     ELEMENT NODE UTILS
 * 
 ***************************/

ElementQueueNode* NewNode(char* element, size_t element_len, mstime_t ttl);

void freeNode(ElementQueueNode* node);


#endif // LAWN_LIB_H
