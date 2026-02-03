#ifndef SETTINGS_H
#define SETTINGS_H

#include "../include/types.h"

// Define max lengths
#define SETTINGS_KEY_LEN 32
#define SETTINGS_VAL_LEN 64
#define SETTINGS_MAX_ENTRIES 20

typedef struct {
    char key[SETTINGS_KEY_LEN];
    char value[SETTINGS_VAL_LEN];
} setting_entry_t;

// Initialize settings (load from default or file)
void settings_init();

// Load settings from a .pset file in the initrd
void settings_load_from_file(const char* filename);

// Get a setting as a string
const char* settings_get(const char* key);

// Get a setting as an integer (dec or hex)
int settings_get_int(const char* key);

// Set a setting (in-memory only for now, as we don't have disk write)
void settings_set(const char* key, const char* value);

#endif
