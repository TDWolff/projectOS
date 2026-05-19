#ifndef TASK_H
#define TASK_H

#include "../include/types.h"

// Priority levels: lower number = higher priority
#define TASK_PRIO_UI     0   // UI / compositor — preempts workers
#define TASK_PRIO_WORKER 1   // background shell, network commands

typedef struct task {
    uint64_t id;
    uint64_t rsp;              // saved stack pointer
    uint8_t  priority;         // TASK_PRIO_* constant
    uint8_t  ticks_remaining;  // countdown until quantum expires
    struct task* next;         // circular linked list
} task_t;

void task_init(void);
void create_task(void (*entry_point)(void));
uint64_t schedule(uint64_t current_rsp);

#endif