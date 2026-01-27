#include "heap.h"
#include "pmm.h"
#include "../lib/stdio.h"

static heap_node_t* head = 0;

void heap_init() {
    // 1. Ask the PMM for one page of physical RAM to start our heap
    void* initial_page = pmm_alloc();
    
    if (!initial_page) {
        kprintf("Heap Error: Failed to allocate initial page!\n");
        return;
    }

    // 2. Setup the first node (The whole page minus the header size)
    head = (heap_node_t*)initial_page;
    head->size = 4096 - sizeof(heap_node_t);
    head->is_free = 1;
    head->next = 0;

    kprintf("Heap Initialized at: %x\n", (uint64_t)initial_page);
}

void* kmalloc(uint64_t size) {
    // Align size to 8 bytes for x86_64 performance
    size = (size + 7) & ~7;

    heap_node_t* current = head;

    while (current) {
        if (current->is_free && current->size >= size) {
            // Can we split this block? 
            // Only split if there's enough room for a new header + at least 8 bytes of data
            if (current->size > size + sizeof(heap_node_t) + 8) {
                heap_node_t* new_node = (heap_node_t*)((uint8_t*)current + sizeof(heap_node_t) + size);
                new_node->size = current->size - size - sizeof(heap_node_t);
                new_node->is_free = 1;
                new_node->next = current->next;

                current->size = size;
                current->next = new_node;
            }

            current->is_free = 0;
            // Return the pointer to the memory AFTER the header
            return (void*)((uint8_t*)current + sizeof(heap_node_t));
        }
        current = current->next;
    }

    kprintf("Heap: Out of memory during kmalloc(%d)!\n", size);
    return 0;
}

void kfree(void* ptr) {
    if (!ptr) return;

    // Move the pointer back to find the header
    heap_node_t* node = (heap_node_t*)((uint8_t*)ptr - sizeof(heap_node_t));
    node->is_free = 1;

    // Simple Coalescing: merge with next block if it is also free
    if (node->next && node->next->is_free) {
        node->size += node->next->size + sizeof(heap_node_t);
        node->next = node->next->next;
    }
}