#include "shell.h"
// NOTE: Shell is logic-only now. Keep VGA out of this module.
// #include "vga.h"
#include "timer.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../mem/heap.h"
#include "../cpu/idt.h"
#include "../fs/initrd.h"
#include "../fs/pfs/pfs.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "mouse.h"
#include "../lib/settings.h"

// Shell output sink: lets the windowed terminal display shell I/O.
static void (*g_shell_putc)(char c, void* user) = 0;
static void* g_shell_putc_user = 0;

void shell_set_output_sink(void (*putc_cb)(char c, void* user), void* user) {
    g_shell_putc = putc_cb;
    g_shell_putc_user = user;
}

void shell_get_output_sink(void (**out_putc_cb)(char c, void* user), void** out_user) {
    if (out_putc_cb) *out_putc_cb = g_shell_putc;
    if (out_user) *out_user = g_shell_putc_user;
}

static void shell_out_char(char c) {
    if (g_shell_putc) g_shell_putc(c, g_shell_putc_user);
}

static void shell_out_str(const char* s) {
    if (!s) return;
    for (int i = 0; s[i]; i++) shell_out_char(s[i]);
}

void shell_print_prompt() {
    const char* username = settings_get("username");
    if (!username || !username[0]) username = "root";
    shell_out_str(username);
    shell_out_str(" % ");
}

// List of files to exclude from user view/access
static const char* protected_files[] = {
    "settings.pset",
    "kernel.bin", // Usually protected anyway, but good to list
    "limine.conf",
    0 // Null terminator
};

static bool is_file_protected(const char* name) {
    for (int i = 0; protected_files[i]; i++) {
        if (strcmp(name, protected_files[i]) == 0) return true;
    }
    return false;
}

// --- /user listing helpers ---

static int g_ls_user_count = 0;

static bool shell_pfs_ls_cb(const char* name, bool is_dir, void* user) {
    (void)user;
    if (!name || !name[0]) return true;
    if (is_file_protected(name)) return true;

    shell_out_str("  ");
    shell_out_str(name);
    if (is_dir) shell_out_str("/");
    shell_out_str("\n");
    g_ls_user_count++;
    return true;
}

static bool shell_ls_user_root() {
    g_ls_user_count = 0;
    if (!pfs_list_user_root(shell_pfs_ls_cb, 0)) return false;
    if (g_ls_user_count == 0) shell_out_str("  (empty)\n");
    return true;
}

#define MAX_COMMAND_LEN 128
static char command_buffer[MAX_COMMAND_LEN];
static int buffer_idx = 0;

void shell_init() {
    memset(command_buffer, 0, MAX_COMMAND_LEN);
    buffer_idx = 0;

    // VGA text terminal is no longer used. Background and desktop rendering
    // are handled by the compositor/windowing layer.

    // Shell no longer draws its own window. Rendering/hosting is handled by the
    // window manager (or by compositor/system UI) exclusively.

    // Print initial prompt to the current output sink.
    shell_print_prompt();
}

void shell_check_click() {
    // No-op: shell no longer owns any window chrome or click handling.
}

void execute_command(char* input) {
    // 1. Help
    if (strcmp(input, "help") == 0) {
        shell_out_str("ls, cat <file>, pfs, clear, ticks, divzero, echo <text>, run <program>\n");
        shell_out_str("  - cat <file>: reads initrd file OR /user/<file> if you pass /user/NAME.EXT\n");
        shell_out_str("  - pfs: shows persistence (/user) mount status\n");
    } 
    // PFS status
    else if (strcmp(input, "pfs") == 0) {
        const pfs_state_t* st = pfs_get_state();
        shell_out_str("PFS: ");
        if (!st || !st->has_disk) {
            shell_out_str("no disk\n");
        } else {
            shell_out_str("disk OK, /user=");
            shell_out_str(st->user_mounted ? "mounted\n" : "not mounted\n");
        }

        // Print verbose probe log into the terminal window's scrollback.
        pfs_putc_fn putc_cb = 0;
        void* user = 0;
        shell_get_output_sink((void (**)(char, void*))&putc_cb, &user);
        if (putc_cb) {
            pfs_debug_probe_and_print_to(putc_cb, user);
        } else {
            // Fallback if no terminal window is attached.
            pfs_debug_probe_and_print();
        }
    }
    // 1. RUN (Execute Program) - Quick hack parsing
    else if (input[0] == 'r' && input[1] == 'u' && input[2] == 'n' && input[3] == ' ') {
        char* filename = input + 4;

        if (is_file_protected(filename)) {
            shell_out_str("Error: Access Denied (Protected File)\n");
            return;
        }

        file_t* f = initrd_open(filename);

        if (f) {
            shell_out_str("Loading program '");
            shell_out_str(filename);
            shell_out_str("'...\n");
            
            // 1. Create a new address space for the app
            extern uint64_t p4_table[]; // Access kernel table
            uint64_t* app_pagemap = vmm_create_address_space();

            // 2. Allocate and map the app to 4GB (User Space Territory)
            // We'll calculate how many pages we need
            uint64_t num_pages = (f->size + PAGE_SIZE - 1) / PAGE_SIZE;
            uint64_t app_virtual_base = 0x100000000;

            for (uint64_t i = 0; i < num_pages; i++) {
                void* physical_page = pmm_alloc();
                // Map the app virtual address to the allocated physical page
                // Crucial: Use PAGE_USER flag so the app can access its own memory!
                vmm_map_page(app_pagemap, app_virtual_base + (i * PAGE_SIZE), (uint64_t)physical_page, PAGE_WRITABLE | PAGE_USER);
                
                // Copy data to the physical page
                uint64_t copy_size = (i == num_pages - 1) ? (f->size % PAGE_SIZE) : PAGE_SIZE;
                if (copy_size == 0) copy_size = PAGE_SIZE; // Handle exact multiples
                memcpy(physical_page, (void*)(f->address + (i * PAGE_SIZE)), copy_size);
            }

            // 3. Switch to the new page map and jump!
            uint64_t kernel_pagemap;
            __asm__ volatile("mov %%cr3, %0" : "=r"(kernel_pagemap));

            vmm_switch_pagemap(app_pagemap);

            void (*app_entry)(void) = (void*)app_virtual_base;
            app_entry();
            
            // 4. Return to kernel address space
            vmm_switch_pagemap((uint64_t*)kernel_pagemap);

            // 5. UI redraw is handled elsewhere (compositor/window manager).
            // The shell is logic-only and shouldn't call rendering functions directly.
            shell_out_str("Program finished.\n");
        } else {
            shell_out_str("Program not found: ");
            shell_out_str(filename);
            shell_out_str("\n");
        }
    }
    // 2. LS (List Files)
    else if (strcmp(input, "ls") == 0 ||
             strcmp(input, "ls /user") == 0 ||
             strcmp(input, "ls /user/") == 0) {
        // If listing /user, use PFS/FAT32.
        if (strcmp(input, "ls /user") == 0 || strcmp(input, "ls /user/") == 0) {
            shell_out_str("/user:\n");
            if (!pfs_get_state() || !pfs_get_state()->user_mounted) {
                shell_out_str("  (not mounted)\n");
                return;
            }

            if (!shell_ls_user_root()) {
                shell_out_str("  (error reading directory)\n");
            }
            return;
        }

        file_t* files = initrd_get_files();
        for(int i=0; i<MAX_FILES; i++) {
            if(files[i].exists && !is_file_protected(files[i].name)) {
        shell_out_str(files[i].name);
        shell_out_str("\n");
            }
        }
    }
    // 3. CAT (Read File) - Quick hack parsing
    else if (input[0] == 'c' && input[1] == 'a' && input[2] == 't' && input[3] == ' ') {
        char* filename = input + 4; // Skip "cat "

        if (is_file_protected(filename)) {
            shell_out_str("Error: Access Denied (Protected File)\n");
            return;
        }

        // If the user asks for /user/<file>, read using PFS.
        if (filename[0] == '/' && filename[1] == 'u' && filename[2] == 's' && filename[3] == 'e' && filename[4] == 'r' && filename[5] == '/') {
            uint8_t* buf = 0;
            uint32_t sz = 0;
            if (pfs_read_user_file(filename, &buf, &sz)) {
                shell_out_str("\n");
                for (uint32_t i = 0; i < sz; i++) shell_out_char((char)buf[i]);
                kfree(buf);
                shell_out_str("\n");
            } else {
                shell_out_str("File not found (or /user not mounted): ");
                shell_out_str(filename);
                shell_out_str("\n");
            }
        } else {
            file_t* f = initrd_open(filename);
            if (f) {
                shell_out_str("\n");
                char* content = (char*)f->address;
                for(uint64_t i=0; i < f->size; i++) {
                    shell_out_char(content[i]);
                }
            } else {
                shell_out_str("File not found: ");
                shell_out_str(filename);
            }
            shell_out_str("\n"); // Newline after content for clean lines
        }
    }
    else if (strcmp(input, "clear") == 0) {
    // Shell is logic-only: clear just emits a couple newlines for now.
    // (A future terminal UI can implement a real clear-screen escape.)
    shell_out_str("\n\n");
    shell_print_prompt();
        return;
    } 
    else if (strcmp(input, "ticks") == 0) {
        // kprintf("\nSystem ticks: %d", get_ticks());
    }
    else if (strcmp(input, "divzero") == 0) {
        // kprintf("\nDividing by zero...");
        volatile int a = 1;
        volatile int b = 0;
        volatile int c = a / b;
        (void)c;
    }
    else if (input[0] == 'e' && input[1] == 'c' && input[2] == 'h' && input[3] == 'o') {
        // kprintf("\n%s", input + 5); 
    }
    else if (strlen(input) > 0) {
        // kprintf("\nUnknown: %s", input);
    }
    // kprintf("\nroot %% "); // Print prompt with newline for next line
}

void shell_update(char c) {
    shell_check_click();

    if (c == '\n') {
        command_buffer[buffer_idx] = '\0';
        shell_out_str("\n");
        execute_command(command_buffer);
        memset(command_buffer, 0, MAX_COMMAND_LEN);
        buffer_idx = 0;
    shell_print_prompt();
    } else if (c == '\b') {
        if (buffer_idx > 0) {
            buffer_idx--;
            command_buffer[buffer_idx] = 0;
            // kprint_char('\b');
            shell_out_char('\b');
        }
    } else {
        if (buffer_idx < MAX_COMMAND_LEN - 1) {
            command_buffer[buffer_idx++] = c;
            // kprint_char(c);
            shell_out_char(c);
        }
    }
}