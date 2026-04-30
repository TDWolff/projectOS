#include "pmm.h"
#include "../include/multiboot2.h"
#include "../lib/string.h"
#include "../lib/stdio.h"

#define PAGE_SIZE 4096
#define BUMP_MEM  0x400000 // 4MB: Protect everything below this (BIOS + Kernel + Stack)

uint8_t* bitmap;
uint64_t max_pages;
uint64_t bitmap_size;

void pmm_init(void* mb_info) {
    struct multiboot_tag* tag;
    struct multiboot_tag_mmap* mmap_tag = 0;
    uint64_t highest_address = 0;

    // 1. Find Memory Map Tag
    for (tag = (struct multiboot_tag*)((uint8_t*)mb_info + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) 
    {
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            mmap_tag = (struct multiboot_tag_mmap*)tag;
            break;
        }
    }

    if (!mmap_tag) {
        kprintf("PMM Error: No Memory Map!\n");
        while(1);
    }

    // 2. Calculate Max RAM
    int entries = (mmap_tag->size - sizeof(struct multiboot_tag_mmap)) / mmap_tag->entry_size;
    for (int i = 0; i < entries; i++) {
        if (mmap_tag->entries[i].type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint64_t top = mmap_tag->entries[i].addr + mmap_tag->entries[i].len;
            if (top > highest_address) highest_address = top;
        }
    }

    max_pages = highest_address / PAGE_SIZE;
    bitmap_size = max_pages / 8;

    // 3. Find a safe spot for the Bitmap (Must be above 2MB)
    int found_bitmap_spot = 0;
    for (int i = 0; i < entries; i++) {
        uint64_t entry_start = mmap_tag->entries[i].addr;
        uint64_t entry_len = mmap_tag->entries[i].len;

        if (mmap_tag->entries[i].type == MULTIBOOT_MEMORY_AVAILABLE) {
            // If this chunk starts below 2MB, calculate how much is left above 2MB
            if (entry_start < BUMP_MEM) {
                uint64_t diff = BUMP_MEM - entry_start;
                if (entry_len <= diff) continue; // Entire chunk is in the protected zone
                entry_start += diff;
                entry_len -= diff;
            }

            if (entry_len >= bitmap_size) {
                bitmap = (uint8_t*)entry_start;
                memset(bitmap, 0xFF, bitmap_size); // Mark EVERYTHING as used initially
                found_bitmap_spot = 1;
                break;
            }
        }
    }

    if (!found_bitmap_spot) {
        kprintf("PMM Error: Could not find spot for bitmap!\n");
        while(1);
    }

    // 4. Mark usable RAM as FREE only if it's above 2MB
    uint64_t usable_ram = 0;
    for (int i = 0; i < entries; i++) {
        if (mmap_tag->entries[i].type == MULTIBOOT_MEMORY_AVAILABLE) {
            for (uint64_t j = 0; j < mmap_tag->entries[i].len; j += PAGE_SIZE) {
                uint64_t addr = mmap_tag->entries[i].addr + j;
                
                // SKIP THE PROTECTED ZONE (0 to 2MB)
                if (addr < BUMP_MEM) continue;
                
                // SKIP THE BITMAP ITSELF
                if (addr >= (uint64_t)bitmap && addr < (uint64_t)bitmap + bitmap_size) continue;

                uint64_t page_index = addr / PAGE_SIZE;
                bitmap[page_index / 8] &= ~(1u << (page_index % 8)); // Mark Free
                usable_ram += PAGE_SIZE;
            }
        }
    }

    kprintf("PMM Initialized. Usable RAM: %d MB\n", (uint32_t)(usable_ram / 1024 / 1024));
    kprintf("Bitmap Location: %x\n", (uint64_t)bitmap);
}

void* pmm_alloc() {
    // Start loop from a higher index to avoid low memory addresses
    for (uint64_t i = 1024; i < max_pages; i++) {
        if (!(bitmap[i / 8] & (1u << (i % 8)))) {
            bitmap[i / 8] |= (1u << (i % 8));
            return (void*)(i * PAGE_SIZE);
        }
    }
    kprintf("PMM: OUT OF MEMORY!\n"); // Debug print
    return 0; 
}

void pmm_free(void* ptr) {
    uint64_t page_index = (uint64_t)ptr / PAGE_SIZE;
    bitmap[page_index / 8] &= ~(1u << (page_index % 8));
}