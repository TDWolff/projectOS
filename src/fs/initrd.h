#ifndef INITRD_H
#define INITRD_H

#include "../include/types.h"

#define MAX_FILES 10

typedef struct {
    char name[32];
    uint64_t address;
    uint64_t size;
    uint8_t exists;
} file_t;

// Scans memory and populates the file list
void initrd_init(void* mb_info);

// Returns the list of files
file_t* initrd_get_files();

// Finds a file by name (returns 0 if not found)
file_t* initrd_open(const char* name);

#endif