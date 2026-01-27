#include "initrd.h"
#include "../include/multiboot2.h"
#include "../lib/stdio.h"

void read_initrd(void* mb_info) {
    struct multiboot_tag* tag;
    
    kprintf("FS: Scanning for modules...\n");

    for (tag = (struct multiboot_tag*)((uint8_t*)mb_info + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) 
    {
        // Check if this tag describes a Module (File)
        if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
            struct multiboot_tag_module* mod = (struct multiboot_tag_module*)tag;
            
            kprintf("FS: Found Module: %s\n", mod->cmdline);
            uint32_t size = mod->mod_end - mod->mod_start;
            kprintf("FS: Address: %x, Size: %d bytes\n", mod->mod_start, size);
            
            kprintf("--- START FILE ---\n");
            
            // Pointer to the raw file data in memory
            char* file_content = (char*)(uint64_t)mod->mod_start;
            
            // Print the file character by character
            for(uint32_t i = 0; i < size; i++) {
                kprintf("%c", file_content[i]);
            }
            kprintf("\n--- END FILE ---\n");
        }
    }
}