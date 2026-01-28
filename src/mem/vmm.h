#ifndef VMM_H
#define VMM_H

#include "../include/types.h"

#define PAGE_SIZE 4096

// Page Table Entry Flags
#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER     (1ULL << 2)

typedef uint64_t pt_entry_t;

void vmm_map_page(uint64_t* p4, uint64_t virtual_addr, uint64_t physical_addr, uint8_t flags);
uint64_t* vmm_create_address_space();
void vmm_switch_pagemap(uint64_t* p4);

#endif
