#ifndef PMM_H
#define PMM_H

#include "../include/types.h"

void pmm_init(void* mb_info);
void* pmm_alloc();
void pmm_free(void* ptr);

#endif