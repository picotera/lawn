/* ###################################################################
 *  Lawn - Low Latancy Timer Data-Structure for Large Scale Systems 
 *
 *  Autor: Adam Lev-Libfeld (adam@tamarlabs.com) 2017-2020
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
    return (size_t)k;
}

bool ttl_equal_fn(const void *a, const void *b, void *ctx)
{
    return (size_t)a == (size_t)b;
}


// elements

// based on djb2. see: http://www.cse.yorku.ca/~oz/hash.html
unsigned long string_hash(char *str)
{
    unsigned long hash = 5381;
    int c;

    while (c = *str++)
    {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

size_t elem_hash_fn(const void *k, void *ctx)
{
    return string_hash((char*)k);
}

bool elem_equal_fn(const void *a, const void *b, void *ctx)
{
    return (strcmp((char* )a, (char* )b) == 0);
}


/***************************
 *     Mapping Utilities
 ***************************/

void _removeNodeFromMapping(Lawn* lawn, ElementQueueNode* node)
{
    const void *oldk;
    void *oldv;
    hashmap__delete(lawn->element_nodes, node->element, &oldk, &oldv);
}


void _removeQueueFromMapping(Lawn* lawn, int queue_ttl)
{
    const void* oldk = NULL;
    void* oldv = NULL;
    hashmap__delete(lawn->timeout_queues, &queue_ttl, &oldk, &oldv);
    freeQueue(oldv);
}



ElementQueueNode* _findNodeInMapping(Lawn* lawn, char* element)
{
    void* node;
    bool found = hashmap__find(lawn->element_nodes, element, &node);
    if (found)
    {
        return (ElementQueueNode *)node;
    }
    return NULL;
}


ElementQueueNode* _findQueueInMapping(Lawn* lawn, mstime_t queue_ttl)
{
    void* queue;
    bool found = false;
    const void * key = (const void *)(size_t)queue_ttl;
    found = hashmap__find(lawn->timeout_queues, key, &queue);
    if (found)
    {
        return (ElementQueueNode*)queue;
    }
    return NULL;

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
    return newNode;
}


void freeNode(ElementQueueNode* node)
{
    // free everything else related to the node
    free(node->element);
    free(node);
}


//Creates a new Node and returns pointer to it.
ElementQueueNode* newQueue() {
    TAILQ_HEAD(, element_queue_node) head = 
                                TAILQ_HEAD_INITIALIZER(head);
    TAILQ_INIT(&head); /* Initialize the queue. */
    return &head;
}


void freeQueue(ElementQueueNode* queuehead) {
    if (!queuehead || queuehead == NULL)
        return;
    ElementQueueNode* item = NULL;
    while (item = TAILQ_FIRST(queuehead)) {
            TAILQ_REMOVE(queuehead, item, tqe);
            freeNode(item);
    }
}


// insert a Node at tail of linked queue
void queuePush(ElementQueueNode* queuehead, ElementQueueNode* node)
{
    TAILQ_INSERT_TAIL(queuehead, node, tqe);
}


void _queuePull(Lawn* lawn, ElementQueueNode* queuehead, ElementQueueNode* node)
{
    if (queuehead == NULL)
        return;

    TAILQ_REMOVE(queuehead, node, tqe);

    if ((lawn != NULL) && (TAILQ_EMPTY(queuehead)))
    {
        // remove last item and delete from mapping if
        // also got a lawn, remove empty queue from mapping
        _removeQueueFromMapping(lawn, node->ttl_queue);
    }
}


// pull and return the element at the first location
ElementQueueNode* _queuePop(Lawn* lawn, ElementQueueNode* queuehead) 
{
    if ((queuehead == NULL) || (TAILQ_EMPTY(queuehead))) // if queue empty
    {
        return NULL;
    }



    // save current head
    ElementQueueNode* node = TAILQ_FIRST(queuehead);
    TAILQ_REMOVE(queuehead, node, tqe);

    if ((lawn != NULL) && (TAILQ_EMPTY(queuehead)))
    {
        // remove last item and delete from mapping if
        // also got a lawn, remove empty queue from mapping
        _removeQueueFromMapping(lawn, node->ttl_queue);
    }
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


int _addNodeToMapping(Lawn* lawn, ElementQueueNode* node)
{
    if (node != NULL)
    {
        const void *oldk;
        void *oldv;
        const void * key = (const void*)(node->element);
        int err = hashmap__set(lawn->element_nodes,
                               key, node,
                               &oldk, &oldv);
        if (err) return LAWN_ERR;

        if (node->expiration < lawn->next_expiration){
            lawn->next_expiration = node->expiration;
        }
    }
    return LAWN_OK;
}


void _removeNode(Lawn* lawn, ElementQueueNode* node)
{
    ElementQueue* queue = _findQueueInMapping(lawn, node->ttl_queue);

    if (queue != NULL)
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
        if (entry != NULL && entry->value != NULL)
        {
            freeQueue((ElementQueue*)entry->value);
        }
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
    return hashmap__size(lawn->timeout_queues);
}


/*
 * Insert ttl for a new key or update an existing one
 * @return LAWN_OK on success, LAWN_ERR on error
 */
int set_element_ttl(Lawn* lawn, char* element, size_t len, mstime_t ttl_ms){
    //create new node
    ElementQueueNode* new_node = NewNode(element, len, ttl_ms);
    // find correct ttl queue for node
    ElementQueueNode* new_queue = _findQueueInMapping(lawn, ttl_ms);
    if (new_queue == NULL){
        // queue missing, create new it and add to mapping
        new_queue = newQueue();
        const void * key = (const void *)(size_t)ttl_ms;
        void * value = (void *) new_queue;
        int err = hashmap__add(lawn->timeout_queues, key, value);
        if (err) return LAWN_ERR;
    }

    // add node to ttl queue and mapping
    queuePush(new_queue, new_node);
    return _addNodeToMapping(lawn, new_node);
}


int add_new_node(Lawn* lawn, char* element, size_t len, mstime_t ttl_ms){
    return set_element_ttl(lawn, element, len, ttl_ms);
}


/*
 * Get the expiration value for the given key
 * @return datetime of expiration (in milliseconds) on success, -1 on error
 */
mstime_t get_element_exp(Lawn* lawn, char* key){
    ElementQueueNode* node = _findNodeInMapping(lawn, key);
    if (node != NULL)
    {
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
    ElementQueueNode* node = _findNodeInMapping(lawn, key);
    if (node != NULL){
        _removeNode(lawn, node);
        freeNode(node);
    } 

    return LAWN_OK;
}


ElementQueueNode* _get_next_node(Lawn* lawn){
    HashMapEntry *entry;
    mstime_t head_node_exp;
    ElementQueue* queuehead;
    ElementQueueNode* node,
    ElementQueueNode* next_node = NULL;
    int bkt;
    hashmap__for_each_entry(lawn->timeout_queues, entry, bkt) {
        queuehead = (ElementQueueNode*)entry->value;
        if (!TAILQ_EMPTY(queuehead)) {
            node = TAILQ_FIRST(queuehead);
            head_node_exp = node->expiration;
            if (next_node == NULL || head_node_exp < next_node->expiration)
            {
                next_node = node;
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

ElementQueueNode* pop_expired(Lawn* lawn) {
    ElementQueueNode* retval = newQueue();
    mstime_t now = current_time_ms() + LAWN_LATANCY_PADDING_MS;
    if (now < lawn->next_expiration){
            return retval;
    }


    HashMapEntry *entry = NULL;
    ElementQueueNode* queuehead = NULL;
    ElementQueueNode* next_node = NULL;
    ElementQueueNode* node = NULL;
    int bkt = 0;
    hashmap__for_each_entry(lawn->timeout_queues, entry, bkt) {
        queuehead = (ElementQueueNode*)entry->value;
        while (!TAILQ_EMPTY(queuehead)) {
            node = TAILQ_FIRST(queuehead);
            if (node->expiration <= now)
            {
                ElementQueueNode* node = _queuePop(lawn, queuehead);
                _removeNodeFromMapping(lawn, node);
                queuePush(retval, node);
            }
            else
            {
                if ((lawn->next_expiration == 0) ||
                    (node->expiration < lawn->next_expiration))
                    lawn->next_expiration = node->expiration;
                break;
            }
        }
    }
    return retval;
}

