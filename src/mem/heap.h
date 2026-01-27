#ifndef HEAP_H
#define HEAP_H

#include "../include/types.h"

// Define a header for every chunk of memory on the heap
typedef struct heap_node {
    uint64_t size;           // Size of this data chunk
    uint8_t  is_free;        // 1 if free, 0 if used
    struct heap_node* next;  // Link to the next chunk
} heap_node_t;

void  heap_init();
void* kmalloc(uint64_t size);
void  kfree(void* ptr);

#endif