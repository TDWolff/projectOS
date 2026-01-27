#include "initrd.h"
#include "../include/multiboot2.h"
#include "../lib/string.h"

static file_t files[MAX_FILES];

void initrd_init(void* mb_info) {
    struct multiboot_tag* tag;
    int file_count = 0;
    memset(files, 0, sizeof(files));

    for (tag = (struct multiboot_tag*)((uint8_t*)mb_info + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) 
    {
        if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
            if (file_count >= MAX_FILES) break;

            struct multiboot_tag_module* mod = (struct multiboot_tag_module*)tag;
            file_t* f = &files[file_count];
            
            // Try to copy name from cmdline
            int i = 0;
            char* cmdline = (char*)mod->cmdline;
            if (cmdline[0] != 0) {
                while(cmdline[i] != 0 && i < 31) {
                    f->name[i] = cmdline[i];
                    i++;
                }
                f->name[i] = 0;
            }

            // If name is still empty, auto-assign file_0, file_1, etc.
            if (f->name[0] == 0) {
                f->name[0] = 'f'; f->name[1] = 'i'; f->name[2] = 'l'; f->name[3] = 'e';
                f->name[4] = '_'; f->name[5] = '0' + file_count; f->name[6] = 0;
            }

            f->address = (uint64_t)mod->mod_start;
            f->size = mod->mod_end - mod->mod_start;
            f->exists = 1;
            file_count++;
        }
    }
}

file_t* initrd_get_files() { return files; }

file_t* initrd_open(const char* name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].exists && strcmp(files[i].name, name) == 0) return &files[i];
    }
    return 0; 
}