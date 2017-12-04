# Lawn API

## Lawn Auditing
`Lawn* newLawn(void);`

Create a new Lawn timer store.


`void freeLawn(Lawn* dehy);`

Free all data and memory of a Lawn data store 


`size_t ttl_count(Lawn* dehy);`

Return the number of uniqe ttl entries in the lawn


`mstime_t next_at(Lawn* dehy);`

Return the closest element expiration datetime (in milliseconds), or -1 if DS is empty


## TTL Manipulation

`int set_element_ttl(Lawn* dehy, char* element, size_t len, mstime_t ttl_ms);`

Insert ttl for a new element or update an existing one. Returns LAWN_OK on success, LAWN_ERR on error.

`mstime_t get_element_exp(Lawn* dehy, char* element);`

Get the expiration value for the given element. Returns datetime of expiration (in milliseconds) on success, -1 on error


`int del_element_exp(Lawn* dehy, char* element);`

Remove TTL from the lawn for the given element. Returns `LAWN_OK`


`ElementQueueNode* pop_next(Lawn* dehy);`

Remove and return the node for the element with the closest expiration datetime


`ElementQueue* pop_expired(Lawn* dehy);`

return a queue of all exired element nodes.


## Queue Utilities

`void freeQueue(ElementQueue* queue);`

Free all data and memory of a queue retuned from the `pop_expired` method.



`void queuePush(ElementQueue* queue, ElementQueueNode* node);`

Add a given `ElementQueueNode` to a queue retuned from the `pop_expired` method.



`ElementQueueNode* queuePop(ElementQueue* queue);`

Remove and return the first `ElementQueueNode` from a queue retuned from the `pop_expired` method.



## Element Node Utilities

`ElementQueueNode* NewNode(char* element, size_t element_len, mstime_t ttl);`

Create an `ElementQueueNode`.

`void freeNode(ElementQueueNode* node);`

Free all data and memory of an `ElementQueueNode`.