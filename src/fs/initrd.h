#ifndef INITRD_H
#define INITRD_H

#include "../include/types.h"

// Scans the Multiboot info for module tags (files loaded by Limine)
void read_initrd(void* mb_info);

#endif