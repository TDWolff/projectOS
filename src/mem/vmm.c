#include "vmm.h"
#include "pmm.h"
#include "../lib/string.h"
#include "../lib/stdio.h"

extern uint64_t p4_table[];

void vmm_map_page(uint64_t* p4, uint64_t virtual_addr, uint64_t physical_addr, uint8_t flags) {
    uint64_t p4_idx = (virtual_addr >> 39) & 0x1FF;
    uint64_t p3_idx = (virtual_addr >> 30) & 0x1FF;
    uint64_t p2_idx = (virtual_addr >> 21) & 0x1FF;
    uint64_t p1_idx = (virtual_addr >> 12) & 0x1FF;

    // P3 - Allocate new if not present
    if (!(p4[p4_idx] & PAGE_PRESENT)) {
        uint64_t* new_p3 = pmm_alloc();
        memset(new_p3, 0, PAGE_SIZE);
        p4[p4_idx] = (uint64_t)new_p3 | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    uint64_t* p3 = (uint64_t*)(p4[p4_idx] & ~0xFFF);

    // P2 - Allocate new if not present
    if (!(p3[p3_idx] & PAGE_PRESENT)) {
        uint64_t* new_p2 = pmm_alloc();
        memset(new_p2, 0, PAGE_SIZE);
        p3[p3_idx] = (uint64_t)new_p2 | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    
    // Safety check: Is this a Huge Page?
    if (p3[p3_idx] & (1 << 7)) {
        kprintf("VMM Error: Trying to map over a Huge Page in P3 range\n");
        return;
    }
    uint64_t* p2 = (uint64_t*)(p3[p3_idx] & ~0xFFF);

    // P1 - Allocate new if not present
    if (!(p2[p2_idx] & PAGE_PRESENT)) {
        uint64_t* new_p1 = pmm_alloc();
        memset(new_p1, 0, PAGE_SIZE);
        p2[p2_idx] = (uint64_t)new_p1 | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }

    if (p2[p2_idx] & (1 << 7)) {
        // Crucial fix: your kernel uses 2MB huge pages (bit 7 set)
        // We cannot dive into P1 if the P2 entry is a 2MB page.
        // For now, we'll just allow mapping in the P2 level for the app,
        // or ensure the app's tables are separate from the kernel's.
        kprintf("VMM Error: Trying to map over a Huge Page at %x\n", virtual_addr);
        return;
    }
    uint64_t* p1 = (uint64_t*)(p2[p2_idx] & ~0xFFF);

    p1[p1_idx] = physical_addr | PAGE_PRESENT | flags;
}

uint64_t* vmm_create_address_space() {
    uint64_t* p4 = pmm_alloc();
    memset(p4, 0, PAGE_SIZE);
    
    // Mirror the kernel's identity map. 
    uint64_t* kernel_p3 = (uint64_t*)(p4_table[0] & ~0xFFF);
    uint64_t* app_p3 = pmm_alloc();
    memset(app_p3, 0, PAGE_SIZE);
    p4[0] = (uint64_t)app_p3 | PAGE_PRESENT | PAGE_WRITABLE;

    // Copy the kernel's P3 entries (the 8GB identity map)
    for (int i = 0; i < 512; i++) {
        app_p3[i] = kernel_p3[i];
    }

    return p4;
}

void vmm_switch_pagemap(uint64_t* p4) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(p4));
}
