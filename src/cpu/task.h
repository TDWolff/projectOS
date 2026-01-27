#ifndef TASK_H
#define TASK_H

#include "../include/types.h"

typedef struct task {
    uint64_t id;
    uint64_t rsp;         // The saved stack pointer for this task
    struct task* next;    // Next task in the circular linked list
} task_t;

void task_init();
void create_task(void (*entry_point)());
uint64_t schedule(uint64_t current_rsp);

#endif