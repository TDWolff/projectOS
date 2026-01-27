#include "initrd.h"
#include "../include/multiboot2.h"
#include "../lib/stdio.h"
#include "../lib/string.h"

static file_t files[MAX_FILES];

char get_digit(int i) { return '0' + i; }

void initrd_init(void* mb_info) {
    struct multiboot_tag* tag;
    int file_count = 0;
    
    memset(files, 0, sizeof(files));

    kprintf("FS: Scanning Multiboot Tags...\n");

    for (tag = (struct multiboot_tag*)((uint8_t*)mb_info + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) 
    {
        if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
            // Found a module tag!
            // Let's do a raw dump of the first 32 bytes of this tag to see where the text is.
            kprintf("FS: Found Tag Type 3 (Module). Raw Hex Dump:\n");
            uint8_t* raw = (uint8_t*)tag;
            for(int k=0; k<32; k++) {
                kprintf("%x ", raw[k]);
            }
            kprintf("\n");

            // Manually point to offset 16 (where the string SHOULD be)
            char* raw_string = (char*)((uint8_t*)tag + 16);
            kprintf("FS: String at offset 16: '%s'\n", raw_string);

            // Now try using the struct
            struct multiboot_tag_module* mod = (struct multiboot_tag_module*)tag;
            file_t* f = &files[file_count];

            // Use the Manual Offset just in case the struct is misaligned
            char* cmdline_ptr = (char*)((uint8_t*)tag + 16);
            
            // Copy name
            int i = 0;
            while(cmdline_ptr[i] != 0 && i < 31) {
                f->name[i] = cmdline_ptr[i];
                i++;
            }
            f->name[i] = 0;

            // Fallback
            if (i == 0) {
                f->name[0] = 'f'; f->name[1] = 'i'; f->name[2] = 'l'; f->name[3] = 'e';
                f->name[4] = '_'; f->name[5] = get_digit(file_count); f->name[6] = 0;
            }

            f->address = (uint64_t)mod->mod_start;
            f->size = mod->mod_end - mod->mod_start;
            f->exists = 1;

            kprintf("FS: Registered '%s' (%d bytes)\n", f->name, (uint32_t)f->size);
            file_count++;
        }
    }
}

file_t* initrd_get_files() { return files; }

file_t* initrd_open(const char* name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].exists && strcmp(files[i].name, name) == 0) {
            return &files[i];
        }
    }
    return 0; 
}