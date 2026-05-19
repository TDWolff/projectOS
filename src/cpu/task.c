#include "task.h"
#include "idt.h" 
#include "../mem/heap.h"
#include "../mem/pmm.h" // <--- ADDED: Needed for pmm_alloc
#include "../lib/string.h"
#include "../lib/stdio.h"

// Quantum lengths at 1000 Hz: UI gets 4ms, workers get 1ms.
#define QUANTUM_HIGH  4
#define QUANTUM_LOW   1

static task_t* current_task = 0;
static uint64_t next_pid = 1;

void task_init(void) {
    current_task = (task_t*)kmalloc(sizeof(task_t));
    if (!current_task) kpanic(0, "TASK_INIT_MALLOC_FAIL");

    current_task->id              = next_pid++;
    current_task->priority        = TASK_PRIO_UI;
    current_task->ticks_remaining = QUANTUM_HIGH;
    current_task->next            = current_task;
}

void create_task(void (*entry_point)(void)) {
    task_t* new_task = (task_t*)kmalloc(sizeof(task_t));
    if (!new_task) kpanic(0, "TASK_MALLOC_FAIL");

    new_task->id              = next_pid++;
    new_task->priority        = TASK_PRIO_WORKER;
    new_task->ticks_remaining = QUANTUM_LOW;

    uint64_t* stack = (uint64_t*)pmm_alloc();
    if (!stack) kpanic(0, "TASK_STACK_FAIL");
    memset(stack, 0, 4096);

    uint64_t* rsp = (uint64_t*)((uint8_t*)stack + 4096);

    // IRETQ frame
    // RSP must be 8-byte misaligned (16n+8) at function entry per x86-64 ABI,
    // simulating a CALL having pushed the return address. Without this, Clang's
    // SSE-optimised moves on stack locals (movdqa) fault with GPF #13.
    *(--rsp) = 0x10;
    *(--rsp) = (uint64_t)((uint8_t*)stack + 4096 - 8);
    *(--rsp) = 0x202;
    *(--rsp) = 0x08;
    *(--rsp) = (uint64_t)entry_point;

    // error code + vector placeholders
    *(--rsp) = 0;
    *(--rsp) = 0;

    // 15 general-purpose registers
    for (int i = 0; i < 15; i++) *(--rsp) = 0;

    new_task->rsp = (uint64_t)rsp;

    new_task->next        = current_task->next;
    current_task->next    = new_task;
}

// Weighted round-robin: runs current task for its full quantum, then switches.
// Priority-0 (UI) gets QUANTUM_HIGH ticks, priority-1 (worker) gets QUANTUM_LOW.
uint64_t schedule(uint64_t current_rsp) {
    if (!current_task) return current_rsp;

    current_task->rsp = current_rsp;

    if (current_task->ticks_remaining > 1) {
        current_task->ticks_remaining--;
        return current_rsp;  // stay on current task
    }

    // Quantum expired: advance to next task and assign its quantum.
    current_task = current_task->next;
    current_task->ticks_remaining = (current_task->priority == TASK_PRIO_UI)
                                    ? QUANTUM_HIGH : QUANTUM_LOW;

    if (current_task->rsp == 0) kpanic(0, "SCHEDULER_NULL_STACK");
    return current_task->rsp;
}