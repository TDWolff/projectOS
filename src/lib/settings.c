#include "settings.h"
#include "../lib/string.h"
#include "../lib/stdio.h"
#include "../fs/initrd.h"
#include "../mem/heap.h"
#include "colors.h"

static setting_entry_t settings_table[SETTINGS_MAX_ENTRIES];
static int settings_count = 0;

// Internal helper to parse a line: "key=value"
static void parse_line(char* line) {
    if (!line || line[0] == 0 || line[0] == '\n' || line[0] == '#') return;

    int i = 0;
    while (line[i] && line[i] != '=') i++;

    if (line[i] == '=') {
        line[i] = 0; // Split string
        char* key = line;
        char* val = line + i + 1;

        // Strip newline from value if present
        int vlen = strlen(val);
        if (vlen > 0 && val[vlen - 1] == '\n') val[vlen - 1] = 0;

        settings_set(key, val);
    }
}

void settings_init() {
    // Default values
    settings_set("bg_color", "0xFF008080"); // Teal
    settings_set("dock_color", "0xFFFFFF");
    settings_set("topbar_alpha", "180");
    
    // Try to load from "settings.pset"
    settings_load_from_file("settings.pset");
}

void settings_load_from_file(const char* filename) {
    file_t* f = initrd_open(filename);
    if (!f) {
        kprintf("Settings: File '%s' not found.\n", filename);
        return;
    }

    char* content = (char*)f->address;
    uint64_t size = f->size;
    
    // We need to parse line by line.
    // Since we can't easily modify the initrd memory (it might be read-only logically, 
    // or we just want to be safe), let's copy chunks or scan carefully.
    
    // Simple scanner:
    char line_buffer[128];
    int line_idx = 0;

    for (uint64_t i = 0; i < size; i++) {
        char c = content[i];
        
        if (c == '\n' || line_idx >= 127) {
            line_buffer[line_idx] = 0;
            parse_line(line_buffer);
            line_idx = 0;
        } else {
            line_buffer[line_idx++] = c;
        }
    }
    // Parse last line if no newline at end
    if (line_idx > 0) {
        line_buffer[line_idx] = 0;
        parse_line(line_buffer);
    }
    
    kprintf("Settings: Loaded %d entries from '%s'.\n", settings_count, filename);
}

void settings_set(const char* key, const char* value) {
    // 1. Search for existing key to update
    for (int i = 0; i < settings_count; i++) {
        if (strcmp(settings_table[i].key, key) == 0) {
            // Found, update value
            // Safe copy
            int j = 0;
            while (value[j] && j < SETTINGS_VAL_LEN - 1) {
                settings_table[i].value[j] = value[j];
                j++;
            }
            settings_table[i].value[j] = 0;
            return;
        }
    }

    // 2. Add new if space exists
    if (settings_count < SETTINGS_MAX_ENTRIES) {
        // Copy Key
        int j = 0;
        while (key[j] && j < SETTINGS_KEY_LEN - 1) {
            settings_table[settings_count].key[j] = key[j];
            j++;
        }
        settings_table[settings_count].key[j] = 0;

        // Copy Value
        j = 0;
        while (value[j] && j < SETTINGS_VAL_LEN - 1) {
            settings_table[settings_count].value[j] = value[j];
            j++;
        }
        settings_table[settings_count].value[j] = 0;

        settings_count++;
    } else {
        kprintf("Settings: Table full! Cannot set '%s'.\n", key);
    }
}

const char* settings_get(const char* key) {
    for (int i = 0; i < settings_count; i++) {
        if (strcmp(settings_table[i].key, key) == 0) {
            return settings_table[i].value;
        }
    }
    return 0; // Not found
}

int settings_get_int(const char* key) {
    const char* val = settings_get(key);
    if (!val) return 0;

    // Check if hex (0x...) - color_parse handles hex strings, but let's implement a simple parser here 
    // or rely on a helper if available. 
    // Actually, `color_parse` from colors.h is available if we include it.
    // For pure integers, let's look at the string.

    if (val[0] == '0' && (val[1] == 'x' || val[1] == 'X')) {
        // Hex
        return (int)color_parse(val); // Re-use the hex parser from colors lib
    }

    // Decimal
    int res = 0;
    int sign = 1;
    int i = 0;
    if (val[0] == '-') { sign = -1; i++; }
    
    while (val[i]) {
        if (val[i] >= '0' && val[i] <= '9') {
            res = res * 10 + (val[i] - '0');
        }
        i++;
    }
    return res * sign;
}
