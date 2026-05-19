#include "heap.h"
#include "pmm.h"
#include "../lib/stdio.h"

#define HEAP_SIZE_PAGES 4096 // 16 MB

static heap_node_t* head = 0;

void heap_init() {
    // 1. Allocate the first page
    void* heap_start = pmm_alloc();
    
    if (!heap_start) {
        kprintf("Heap Error: Failed to allocate initial page!\n");
        return;
    }

    // 2. Aggressively allocate sequential pages to build a large contiguous heap
    // Since we don't have virtual memory mapping functions in this simple allocator yet,
    // we rely on PMM returning physical contiguous blocks (which works with the linear bitmap allocator).
    for (int i = 1; i < HEAP_SIZE_PAGES; i++) {
        void* next = pmm_alloc();
        if ((uint64_t)next != (uint64_t)heap_start + (i * 4096)) {
             kprintf("Heap Panic: Non-contiguous memory allocated at index %d\n", i);
             while(1);
        }
    }

    // 3. Setup the first node (The whole block minus the header size)
    head = (heap_node_t*)heap_start;
    head->size = (HEAP_SIZE_PAGES * 4096) - sizeof(heap_node_t);
    head->is_free = 1;
    head->next = 0;

    kprintf("Heap Initialized: 16 MB at %x\n", (uint64_t)heap_start);
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

    heap_node_t* node = (heap_node_t*)((uint8_t*)ptr - sizeof(heap_node_t));
    node->is_free = 1;

    // Forward coalesce: merge with the next block if free.
    if (node->next && node->next->is_free) {
        node->size += node->next->size + sizeof(heap_node_t);
        node->next = node->next->next;
    }

    // Backward coalesce: find the block immediately before this one and
    // merge into it if it's free. Without this, repeated alloc/free cycles
    // fragment the heap into unusable small blocks.
    heap_node_t* prev = 0;
    heap_node_t* cur = head;
    while (cur && cur != node) {
        prev = cur;
        cur = cur->next;
    }
    if (prev && prev->is_free) {
        prev->size += node->size + sizeof(heap_node_t);
        prev->next = node->next;
    }
}