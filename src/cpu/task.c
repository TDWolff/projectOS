#include "task.h"
#include "idt.h" 
#include "../mem/heap.h"
#include "../mem/pmm.h" // <--- ADDED: Needed for pmm_alloc
#include "../lib/string.h"
#include "../lib/stdio.h"

static task_t* current_task = 0;
static uint64_t next_pid = 1;

void task_init() {
    current_task = (task_t*)kmalloc(sizeof(task_t));
    if (!current_task) kpanic(0, "TASK_INIT_MALLOC_FAIL");
    
    current_task->id = next_pid++;
    current_task->next = current_task; 
}

void create_task(void (*entry_point)()) {
    // 1. Allocate the small task structure from the Heap
    task_t* new_task = (task_t*)kmalloc(sizeof(task_t));
    if (!new_task) kpanic(0, "TASK_MALLOC_FAIL");
    
    new_task->id = next_pid++;

    // 2. Allocate the Stack directly from Physical Memory
    // The heap is too small for a full page, and stacks should be page-aligned anyway.
    uint64_t* stack = (uint64_t*)pmm_alloc();
    if (!stack) kpanic(0, "TASK_STACK_FAIL");
    
    // Clean the stack memory
    memset(stack, 0, 4096);

    // Point to the TOP of the stack (Stacks grow downwards)
    uint64_t* rsp = (uint64_t*)((uint8_t*)stack + 4096);

    // --- IRETQ Frame ---
    *(--rsp) = 0x10;                     // SS
    *(--rsp) = (uint64_t)((uint8_t*)stack + 4096); // RSP
    *(--rsp) = 0x202;                    // RFLAGS
    *(--rsp) = 0x08;                     // CS
    *(--rsp) = (uint64_t)entry_point;    // RIP

    // --- Macro Frame ---
    *(--rsp) = 0;
    *(--rsp) = 0;

    // --- General Registers (15 zeros) ---
    for (int i = 0; i < 15; i++) {
        *(--rsp) = 0;
    }

    new_task->rsp = (uint64_t)rsp;
    
    // Insert into circular list
    new_task->next = current_task->next;
    current_task->next = new_task;
}

uint64_t schedule(uint64_t current_rsp) {
    if (!current_task) return current_rsp;

    current_task->rsp = current_rsp;
    current_task = current_task->next;

    if (current_task->rsp == 0) kpanic(0, "SCHEDULER_NULL_STACK");

    return current_task->rsp;
}