#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "linked_list.h"

//
// Machine Problem 1
// CSCE 313-501
// Base code provided by Texas A&M University
// Edited by Ryan Walters and Garrett Haynes
// September 20, 2015
//

void Init (int M, int b){
    // allocate memory, given by M
    memory_pool = M;
    node_size = b;
    head_pointer = malloc(M);
    free_pointer = (char*)head_pointer;
}

void Destroy (){
    head_pointer = NULL;
    free_pointer = NULL;
    memory_pool = 0;
    node_size = 0;
    nodes = 0;
    free(head_pointer);
}

int Insert (int key, char * value_ptr, int value_len){
    // Check if there is enough space to insert a new node
    if(nodes >= memory_pool / node_size){
        printf("ERROR: There is not enough space available to add node with key: %d\n", key);
        return -1;
    }
    
    // Make sure we can fit the value given
    if(value_len > node_size - (sizeof(key)+sizeof(value_len)+sizeof(free_pointer))){
        printf("ERROR: Insertion aborted because size of the value given is too large\n");
        return -1;
    }
    
    // Create new node beginning at free_pointer location
    struct node *new_node = (struct node*)free_pointer;
    new_node->key = key;
    new_node->next = (char*)(free_pointer + node_size);
    //printf("Assigning next node to be %p which is %p + %i", new_node->next, free_pointer, node_size);
    new_node->value_len = value_len;
    
    memcpy((free_pointer + sizeof(struct node)), value_ptr, value_len);
    free_pointer = free_pointer + node_size;
    
    nodes++;
    //    printf ("Inserted: Node = %i, Address = %p, Next Address = %p, Key = %i, Value Len = %i, size = %i\n", nodes, new_node, new_node->next, new_node->key, new_node->value_len, sizeof(struct node));
    return key;
}

int Delete (int key){
    struct node* current_node = head_pointer;
    struct node* previous_node = NULL;
    int deleted = 0;
    
    for(int i = 1; i <= nodes; i++){
        if (current_node->key == key && deleted != 1) {
            if (previous_node != NULL) {
                previous_node->next = current_node->next;
            } else {
                head_pointer = current_node->next;
            }
            nodes--;
            printf("\nDeleted: Node with key: %d\n", key);
            deleted = 1;
        }
        previous_node = current_node;
        current_node = current_node->next;
    }
    
    if(deleted == 1) {
        return key;
    } else {
        printf("\nERROR: Failed to find a node with key: %d\n", key);
        return -1;
    }
}

char* Lookup (int key){
    struct node* current_node = head_pointer;
    
    for(int i = 1; i <= nodes; i++){
        if (current_node->key == key) {
            //printf("Found Key: %i, i: %i", key, i);
            char* location = (char*) current_node + sizeof(struct node*);
            //printf(" returning address: %p\n", location);
            return location;
        }
        current_node = current_node->next;
    }
    return NULL;
}

void PrintList (){
    struct node* current_node = head_pointer;
    for(int i = 1; i <= nodes; i++){
        printf ("Key = %d, Value Len = %d\n", current_node->key, current_node->value_len);
        current_node = current_node->next;
    }
}