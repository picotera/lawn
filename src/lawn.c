/* ###################################################################
 *  Lawn - Low Latancy Timer Data-Structure for Large Scale Systems 
 *  
 *  Autor: Adam Lev-Libfeld (adam@tamarlabs.com) 2017 
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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <inttypes.h>
#include <math.h>
#include "lawn.h"
// #include "utils/millisecond_time.h"


/***************************
 *    Hashmap Utilities
 ***************************/

// ttls

size_t ttl_hash_fn(const void *k, void *ctx)
{
    if (k == NULL || k->head == NULL)
    {
        return -1;
    }
    return k->head->ttl_queue;
}

bool ttl_equal_fn(const void *a, const void *b, void *ctx)
{
    return ttl_hash_fn(a, ctx) == ttl_hash_fn(b, ctx);
}


// elements

// based on djb2. see: http://www.cse.yorku.ca/~oz/hash.html
unsigned long string_hash(unsigned char *str) 
{
    unsigned long hash = 5381;
    int c;

    while (c = *str++)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

size_t elem_hash_fn(const void *k, void *ctx)
{
    return string_hash(k->element);
}

bool elem_equal_fn(const void *a, const void *b, void *ctx)
{
    return strcmp(a->element, b->element);
}


/***************************
 *       Queue Utilities
 ***************************/

//Creates a new Node and returns pointer to it.
ElementQueueNode* NewNode(char* element, size_t element_len, mstime_t ttl)
{
    ElementQueueNode* newNode
        = (ElementQueueNode*)malloc(sizeof(ElementQueueNode));
    newNode->element = strndup(element, element_len);
    newNode->element_len = element_len;
    newNode->ttl_queue = ttl;
    newNode->expiration = current_time_ms() + ttl;
    newNode->next = NULL;
    newNode->prev = NULL;
    return newNode;
}


void freeNode(ElementQueueNode* node)
{
    // free everything else related to the node
    free(node->element);
    free(node);
}


//Creates a new Node and returns pointer to it.
ElementQueue* newQueue() {
    ElementQueue* queue
        = (ElementQueue*)malloc(sizeof(ElementQueue));
    queue->head = NULL;
    queue->tail = NULL;
    queue->len = 0;
    return queue;
}


void freeQueue(ElementQueue* queue) {
    ElementQueueNode* current = queue->head;

    // iterate over queue and find the element that has id = element_id
    while(current != NULL)
    {
        ElementQueueNode* next = current->next; // save next
        freeNode(current);
        current = next;  //move to next node
    }

    free(queue);
}


// insert a Node at tail of linked queue
void queuePush(ElementQueue* queue, ElementQueueNode* node)
{
    if (queue->tail == NULL)
    {
        queue->head = node;
    }
    else
    {
        node->prev = queue->tail;
        queue->tail->next = node;
    }
    queue->tail = node;
    queue->len = (queue->len) + 1;
}


void _queuePull(Lawn* lawn, ElementQueue* queue, ElementQueueNode* node)
{
    if (queue == NULL)
        return;

    if (queue->len == 1)
    {
        // remove last item and delete from mapping
        queue->head = NULL;
        queue->tail = NULL;
        if (lawn != NULL)
        {
            // also got a lawn, remove empty queue from mapping
            _removeQueueFromMapping(lawn, node->ttl_queue);
        }
        return;
    }
    // more than one element
    //hot circuit the node (carefull when pulling from tail or head)
    if ((node == queue->head) || (node->prev == NULL))
    {
        queue->head = queue->head->next;
        queue->head->prev = NULL;
    }
    else
    {
        node->prev->next = node->next;
    }

    if ((node == queue->tail) || (node->next == NULL))
    {
        queue->tail = node->prev;
        queue->tail->next = NULL;
    }
    else
    {
        node->next->prev = node->prev;
    }
    queue->len = queue->len - 1;
}


// pull and return the element at the first location
ElementQueueNode* _queuePop(Lawn* lawn, ElementQueue* queue) {
   if ((queue == NULL) || (queue->head == NULL)) { return NULL; } // if queue empty

   //save current head
   ElementQueueNode* node = queue->head;

   if (queue->len == 1)
   {
       queue->tail = NULL;
       queue->head = NULL;
       if (lawn != NULL)
       {
           hashmap__delete(lawn->timeout_queues, 
                                node->ttl_queue, 
                                NULL, NULL);
       }
   }
   else
   {
       // swap to new head
       queue->head = queue->head->next;
       queue->head->prev = NULL;
   }

   queue->len = queue->len - 1;
   return node;

}


ElementQueueNode* queuePop(ElementQueue* queue) {
    return _queuePop(NULL, queue);
}


/***************************
 *   Lawn Utilities
 ***************************/


Lawn* newLawn(void)
{

    Lawn* lawn = (Lawn*)malloc(sizeof(Lawn));

    lawn->timeout_queues = hashmap__new(ttl_hash_fn, ttl_equal_fn, NULL);
    lawn->element_nodes = hashmap__new(elem_hash_fn, elem_equal_fn, NULL);
    lawn->next_expiration = 0;

    return lawn;
}


void _removeNodeFromMapping(Lawn* lawn, ElementQueueNode* node)
{
    const void *oldk;
    void *oldv;
    hashmap__delete(lawn->element_nodes, node->element, &oldk, &oldv);
}


void _removeQueueFromMapping(Lawn* lawn, int queue_ttl)
{
    const void *oldk;
    ElementQueue *oldv;
    hashmap__delete(lawn->timeout_queues, queue_ttl, &oldk, &oldv);
    freeQueue(oldv);
}


void _addNodeToMapping(Lawn* lawn, ElementQueueNode* node)
{
    if (node != NULL)
    {
        const void *oldk;
        void *oldv;
        int err = hashmap__set(lawn->element_nodes,node->element, node, 
                               &oldk, &oldv);
        if (err) return LAWN_ERR;
        if (node->expiration < lawn->next_expiration){
            lawn->next_expiration = node->expiration;
        }
    }
}


void _removeNode(Lawn* lawn, ElementQueueNode* node)
{
    ElementQueue* queue;
    bool found = hashmap__find(lawn->timeout_queues, node->ttl_queue, &queue);

    if (found && queue != NULL)
    {
        _queuePull(lawn, queue, node);
        _removeNodeFromMapping(lawn, node);
        if (node->expiration <= lawn->next_expiration)
        {
            lawn->next_expiration = 0;
        }
    }
}


void freeLawn(Lawn* lawn)
{
    // clear mapping, the nodes will be free'd when deleting the queues 
    hashmap__free(lawn->element_nodes);

    HashMapEntry *entry, *tmp;
    int bkt;
    hashmap__for_each_entry_safe(lawn->timeout_queues, entry, tmp, bkt) {
        // do we need this instead? _removeQueueFromMapping(lawn, queue_ttl)
        freeQueue((ElementQueue*)entry->value);
    }
    hashmap__free(lawn->timeout_queues);

    free(lawn);
}

/************************************
 * 
 *           DS handling
 * 
 ************************************/

/*
 * @return the number of uniqe ttl entries in the lawn
 */
size_t ttl_count(Lawn* lawn){
    return lawn->element_nodes->cardinality;
}


/*
 * Insert ttl for a new key or update an existing one
 * @return LAWN_OK on success, LAWN_ERR on error
 */
int set_element_ttl(Lawn* lawn, char* element, size_t len, mstime_t ttl_ms){
    //create new node
    ElementQueueNode* new_node = NewNode(element, len, ttl_ms);
    
    // find correct ttl queue for node
    ElementQueue* new_queue;
    bool found = hashmap__find(lawn->timeout_queues, ttl_ms, &new_queue);
    if (!found || new_queue == NULL){
        // queue missing, create new it and add to mapping
        new_queue = newQueue();
        int err = hashmap__add(lawn->timeout_queues, ttl_ms, new_queue);
        if (err) return LAWN_ERR;
    }

    // add node to ttl queue and mapping
    queuePush(new_queue, new_node);
    _addNodeToMapping(lawn, new_node);
    return LAWN_OK;
}


/*
 * Get the expiration value for the given key
 * @return datetime of expiration (in milliseconds) on success, -1 on error
 */
mstime_t get_element_exp(Lawn* lawn, char* key){
    ElementQueueNode* node;
    bool found = hashmap__find(lawn->element_nodes, key, &node);

    if (found && node != NULL){
        return node->expiration;
    }
    return -1;
}


/*
 * Remove TTL from the lawn for the given key
 * @return LAWN_OK
 */
int del_element_exp(Lawn* lawn, char* key)
{
    ElementQueueNode* node;
    bool found = hashmap__find(lawn->element_nodes, key, &node);
    if (found && node != NULL){
        _removeNode(lawn, node);
        freeNode(node);
    } 

    return LAWN_OK;
}


ElementQueueNode* _get_next_node(Lawn* lawn){
    HashMapEntry *entry;
    mstime_t qhead_exp;
    ElementQueue* queue;
    ElementQueueNode* next_node = NULL;
    int bkt;
    hashmap__for_each_entry(lawn->timeout_queues, entry, bkt) {
        queue = (ElementQueue*)entry->value;
        if (queue != NULL && queue->len > 0 && queue->head != NULL) {
            head_node_exp = queue->head->expiration;
            if (next_node == NULL || head_node_exp < next_node->expiration)
            {
                next_node = queue->head;
                if (lawn->next_expiration == 0 ||
                    head_node_exp < lawn->next_expiration){
                    lawn->next_expiration = head_node_exp;
                }
            }
        }
    }
    return next_node;
}

/*
 * @return closest element expiration datetime (in milliseconds), or -1 if empty
 */
mstime_t next_at(Lawn* lawn){
    
    // printf("node: %lu next: %lu diff: %lld\n", node->expiration, lawn->next_expiration, node->expiration - lawn->next_expiration);
    if (lawn->next_expiration == 0){
        ElementQueueNode* next_node = _get_next_node(lawn);
        if (next_node == NULL){
            return -1;
        }else{
            lawn->next_expiration = next_node->expiration;
        }
    }
    return lawn->next_expiration;
    
}


/*
 * Remove the element with the closest expiration datetime from the lawn and return it
 * @return a pointer to the node containing the element with closest 
 * expiration datetime or NULL if the lawn is empty.
 */
ElementQueueNode* pop_next(Lawn* lawn) {
    ElementQueueNode* next_node = _get_next_node(lawn);
        
    if (next_node != NULL) {
        _removeNode(lawn, next_node);
    }
    return next_node;
}

ElementQueue* pop_expired(Lawn* lawn) {
    ElementQueue* retval = newQueue();
    mstime_t now = current_time_ms() + LAWN_LATANCY_PADDING_MS;
    if (now < lawn->next_expiration){
            return retval;
    }


    HashMapEntry *entry;
    mstime_t qhead_exp;
    ElementQueue* queue;
    ElementQueueNode* next_node = NULL;
    int bkt;
    hashmap__for_each_entry(lawn->timeout_queues, entry, bkt) {
        queue = (ElementQueue*)entry->value;
        while (queue != NULL && queue->len > 0 && queue->head != NULL) {
            if (queue->head->expiration <= now)
            {
                ElementQueueNode* node = _queuePop(lawn, queue);
                _removeNodeFromMapping(lawn, node);
                queuePush(retval, node);
            }
            else
            {
                if ((lawn->next_expiration == 0) || 
                    (queue->head->expiration < lawn->next_expiration))
                    lawn->next_expiration = queue->head->expiration;
                break;
            }
        }
    }
    return retval;
}

