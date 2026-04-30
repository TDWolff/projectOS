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

// Networking test command
#include "../net/selftest.h"
// Interface listing
#include "../net/net.h"
#include "../net/arp.h"
#include "../net/ip.h"
#include "../net/icmp.h"
#include "../drivers/net/e1000.h"
#include "timer.h"

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

void sh_putc(char c) {
    shell_out_char(c);
}

void sh_printf(const char* s) {
    if (!s) return;
    for (int i = 0; s[i]; i++) shell_out_char(s[i]);
}

void shell_out_str(const char* s) {
    // Backwards-compatible wrapper.
    sh_printf(s);
}

void shell_print_prompt() {
    const char* username = settings_get("username");
    if (!username || !username[0]) username = "root";
    sh_printf(username);
    sh_printf(" % ");
}

// List of files to exclude from user view/access
static const char* protected_files[] = {
    "settings.pset",
    "kernel.bin", // Usually protected anyway, but good to list
    "limine.conf", // Same thing as kernel.bin
    "terminal.dock",
    0 // Null terminator
};

// Local ASCII-only case-insensitive compare.
// (We don't have a libc `strcasecmp` in the kernel.)
static int to_lower_ascii(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static int stricmp_ascii(const char* a, const char* b) {
    if (!a || !b) return 1;
    int i = 0;
    while (a[i] && b[i]) {
        int ca = to_lower_ascii((unsigned char)a[i]);
        int cb = to_lower_ascii((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
        i++;
    }
    return to_lower_ascii((unsigned char)a[i]) - to_lower_ascii((unsigned char)b[i]);
}

static bool is_file_protected(const char* name) {
    for (int i = 0; protected_files[i]; i++) {
    // FAT32 may return either SFN (usually upper) or LFN (case-preserving)
    // depending on how the file was created on disk; treat protected files
    // as case-insensitive so they remain hidden.
        if (stricmp_ascii(name, protected_files[i]) == 0) return true;
    }
    return false;
}

// --- /user listing helpers ---

static int g_ls_user_count = 0;

static bool shell_pfs_ls_cb(const char* name, bool is_dir, void* user) {
    (void)user;
    if (!name || !name[0]) return true;
    if (is_file_protected(name)) return true;

    sh_printf("  ");
    sh_printf(name);
    if (is_dir) sh_printf("/");
    sh_printf("\n");
    g_ls_user_count++;
    return true;
}

static bool shell_ls_user_root() {
    g_ls_user_count = 0;
    if (!pfs_list_user_root_long(shell_pfs_ls_cb, 0)) return false;
    if (g_ls_user_count == 0) sh_printf("  (empty)\n");
    return true;
}

// --- tiny formatting helpers (shell output sink only) ----------------------

static void sh_print_u64(uint64_t v) {
    char buf[32];
    uint32_t i = 0;
    if (v == 0) {
        sh_putc('0');
        return;
    }
    while (v && i < (uint32_t)(sizeof(buf) - 1)) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i) sh_putc(buf[--i]);
}

static void sh_print_hex32(uint32_t v) {
    const char* hex = "0123456789abcdef";
    char buf[8];
    for (int i = 0; i < 8; i++) {
        buf[7 - i] = hex[v & 0xF];
        v >>= 4;
    }
    for (int i = 0; i < 8; i++) sh_putc(buf[i]);
}

static void sh_print_hex8_2(uint8_t v) {
    const char* hex = "0123456789abcdef";
    sh_putc(hex[(v >> 4) & 0xF]);
    sh_putc(hex[v & 0xF]);
}

static void sh_print_ip4(const uint8_t ip[4]) {
    sh_print_u64(ip[0]); sh_putc('.');
    sh_print_u64(ip[1]); sh_putc('.');
    sh_print_u64(ip[2]); sh_putc('.');
    sh_print_u64(ip[3]);
}

// Parse "a.b.c.d" into out[4]. Returns true on success.
static bool parse_ip4(const char* s, uint8_t out[4]) {
    uint8_t parts[4] = {0};
    int part = 0;
    uint32_t val = 0;
    bool got_digit = false;

    for (int i = 0; ; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            val = val * 10 + (uint32_t)(c - '0');
            if (val > 255) return false;
            got_digit = true;
        } else if (c == '.' || c == '\0') {
            if (!got_digit || part > 3) return false;
            parts[part++] = (uint8_t)val;
            val = 0;
            got_digit = false;
            if (c == '\0') break;
        } else {
            return false;
        }
    }

    if (part != 4) return false;
    for (int i = 0; i < 4; i++) out[i] = parts[i];
    return true;
}

// Send one ICMP echo to target_ip via nic and wait up to timeout_ticks for reply.
// Re-enables interrupts during the wait so get_ticks() advances normally.
static void shell_ping_once(uint8_t target_ip[4], net_nic_interfaces_t* nic) {
    // Decide next-hop: same subnet → ARP target directly, else ARP gateway.
    bool on_subnet = true;
    for (int i = 0; i < 4; i++) {
        if ((target_ip[i] & nic->subnet[i]) != (nic->ip_address[i] & nic->subnet[i])) {
            on_subnet = false;
            break;
        }
    }
    uint8_t* nexthop_ip = on_subnet ? target_ip : nic->gateway;

    // Resolve next-hop MAC via ARP.
    // Re-enable interrupts so the timer ticks while we wait.
    uint8_t dest_mac[6] = {0};
    if (!arp_resolve(nexthop_ip, dest_mac)) {
        arp_lookup(nexthop_ip, nic);
        __asm__ volatile("sti");
        uint64_t deadline = get_ticks() + 200; // 2 seconds at 100 Hz
        while (get_ticks() < deadline) {
            e1000_poll();
            if (arp_resolve(nexthop_ip, dest_mac)) break;
            __asm__ volatile("pause");
        }
        __asm__ volatile("cli");
    }

    if (!arp_resolve(nexthop_ip, dest_mac)) {
        sh_printf("ping: ARP failed - no route to host\n");
        return;
    }

    // Build ICMP echo request.
    const uint16_t ident_be    = BSWAP16(0x1CE0);
    const uint16_t seq_be      = BSWAP16(1);
    const uint32_t payload_len = (uint32_t)sizeof(icmp_echo_hdr_t) + 8;
    const uint32_t ip_len      = (uint32_t)sizeof(ip_packet_t)
                                 + (uint32_t)sizeof(icmp_header_t)
                                 + payload_len;

    ip_packet_t* ip = (ip_packet_t*)kmalloc(ip_len);
    if (!ip) { sh_printf("ping: out of memory\n"); return; }
    memset(ip, 0, ip_len);

    ip->protocol               = 1; // ICMP
    ip->internet_header_length = 5;
    memcpy(ip->destination_protocol_addr, target_ip, 4);

    icmp_header_t*   icmp = (icmp_header_t*)ip->data;
    icmp->type     = 8; // echo request
    icmp->code     = 0;
    icmp->checksum = 0;

    icmp_echo_hdr_t* echo = (icmp_echo_hdr_t*)icmp->data;
    echo->identifier = ident_be;
    echo->sequence   = seq_be;

    uint8_t* p = (uint8_t*)icmp->data + sizeof(icmp_echo_hdr_t);
    for (uint32_t i = 0; i < 8; i++) p[i] = (uint8_t)(0xA0u + i);

    icmp->checksum = ip_calculate_checksum(
        icmp, (int)(sizeof(icmp_header_t) + payload_len));

    icmp_selftest_reset();
    uint64_t tx_before = nic->tx_packets;
    ip_send(ip, (uint16_t)ip_len, target_ip, dest_mac, nic);
    kfree(ip);

    if (nic->tx_packets == tx_before) {
        sh_printf("ping: TX ring full - packet not sent\n");
        return;
    }

    // Wait up to 4 seconds for an ICMP echo reply.
    // Re-enable interrupts so the timer ISR can advance get_ticks().
    bool got_reply = false;
    __asm__ volatile("sti");
    uint64_t deadline = get_ticks() + 400; // 4 seconds at 100 Hz
    while (get_ticks() < deadline) {
        e1000_poll();
        if (icmp_selftest_wait_for_echo_reply(ident_be, seq_be, 1)) {
            got_reply = true;
            break;
        }
        __asm__ volatile("pause");
    }
    __asm__ volatile("cli");

    if (got_reply) {
        sh_printf("ping: reply from ");
        sh_print_ip4(target_ip);
        sh_printf(" ok\n");
    } else {
        sh_printf("ping: request timed out\n");
        sh_printf("  (if pinging internet IPs, try: ping ");
        sh_print_ip4(nic->gateway);
        sh_printf(" first)\n");
    }
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
    sh_printf("ls, cat <file>, pfs, netif, netdevice, nettest, ping <ip>, clear, ticks, divzero, echo <text>, run <program>\n");
    sh_printf("  - cat <file>: reads initrd file OR /user/<file> if you pass /user/NAME.EXT\n");
    sh_printf("  - pfs: shows persistence (/user) mount status\n");
    sh_printf("  - netif: lists network interfaces\n");
    sh_printf("  - netdevice: shows which NIC is selected as primary\n");
    sh_printf("  - nettest: runs networking self-tests (ICMP + UDP echo)\n");
    sh_printf("  - ping <ip>: send ICMP echo to an IP address\n");
    }
    else if (input[0]=='p' && input[1]=='i' && input[2]=='n' && input[3]=='g' && input[4]==' ') {
        const char* ip_str = input + 5;
        uint8_t target[4];
        if (!parse_ip4(ip_str, target)) {
            sh_printf("ping: invalid IP address\n");
        } else {
            net_nic_interfaces_t* nic = net_get_primary_nic();
            if (!nic || (nic->flags & IFF_LOOPBACK)) {
                sh_printf("ping: no real NIC available\n");
            } else {
                sh_printf("ping ");
                sh_print_ip4(target);
                sh_printf("...\n");
                shell_ping_once(target, nic);
            }
        }
    } 
    // PFS status
    else if (strcmp(input, "pfs") == 0) {
        const pfs_state_t* st = pfs_get_state();
        sh_printf("PFS: ");
        if (!st || !st->has_disk) {
            sh_printf("no disk\n");
        } else {
            sh_printf("disk OK, /user=");
            sh_printf(st->user_mounted ? "mounted\n" : "not mounted\n");
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
    else if (strcmp(input, "nettest") == 0) {
        // Loopback-only network self-test suite.
        // (This doesn't require a real NIC driver yet.)
        net_selftest_run();
    }
    else if (strcmp(input, "netif") == 0) {
        sh_printf("Interfaces:\n");

        uint32_t n = net_get_nic_count();
        if (n == 0) {
            sh_printf("  (none)\n");
            return;
        }

        for (uint32_t i = 0; i < n; i++) {
            net_nic_interfaces_t* nic = net_get_nic(i);
            if (!nic) continue;

            sh_printf("  ");
            sh_printf(nic->name);
            sh_printf(" flags=0x");
            sh_print_hex32((uint32_t)nic->flags);

            sh_printf(" ip=");
            sh_print_ip4(nic->ip_address);

            sh_printf(" mac=");
            uint8_t* mac = nic->get_mac_addr ? nic->get_mac_addr() : 0;
            if (mac) {
                sh_print_hex8_2(mac[0]); sh_putc(':');
                sh_print_hex8_2(mac[1]); sh_putc(':');
                sh_print_hex8_2(mac[2]); sh_putc(':');
                sh_print_hex8_2(mac[3]); sh_putc(':');
                sh_print_hex8_2(mac[4]); sh_putc(':');
                sh_print_hex8_2(mac[5]);
            } else {
                sh_printf("(none)");
            }

            sh_printf(" rx=");
            sh_print_u64(nic->rx_packets);
            sh_printf(" tx=");
            sh_print_u64(nic->tx_packets);
            sh_printf("\n");
        }
    }
    else if (strcmp(input, "netdevice") == 0) {
        net_nic_interfaces_t* primary = net_get_primary_nic();

        sh_printf("Primary NIC: ");
        if (primary) {
            sh_printf(primary->name);
        } else {
            sh_printf("(none)");
        }
        sh_printf("\n");

        // Also list what we found, and mark the selected one.
        uint32_t n = net_get_nic_count();
        sh_printf("Discovered NICs:\n");
        if (n == 0) {
            sh_printf("  (none)\n");
            return;
        }

        for (uint32_t i = 0; i < n; i++) {
            net_nic_interfaces_t* nic = net_get_nic(i);
            if (!nic) continue;

            sh_printf("  ");
            if (nic == primary) sh_printf("* ");
            else sh_printf("  ");
            sh_printf(nic->name);
            sh_printf(" flags=0x");
            sh_print_hex32((uint32_t)nic->flags);
            sh_printf("\n");
        }
    }
    // 1. RUN (Execute Program) - Quick hack parsing
    else if (input[0] == 'r' && input[1] == 'u' && input[2] == 'n' && input[3] == ' ') {
        char* filename = input + 4;

        if (is_file_protected(filename)) {
            sh_printf("Error: Access Denied (Protected File)\n");
            return;
        }

        file_t* f = initrd_open(filename);

        if (f) {
            sh_printf("Loading program '");
            sh_printf(filename);
            sh_printf("'...\n");
            
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
            sh_printf("Program finished.\n");
        } else {
            sh_printf("Program not found: ");
            sh_printf(filename);
            sh_printf("\n");
        }
    }
    // 2. LS (List Files)
    else if (strcmp(input, "ls") == 0 ||
             strcmp(input, "ls /user") == 0 ||
             strcmp(input, "ls /user/") == 0) {
        // If listing /user, use PFS/FAT32.
        if (strcmp(input, "ls /user") == 0 || strcmp(input, "ls /user/") == 0) {
            sh_printf("/user:\n");
            if (!pfs_get_state() || !pfs_get_state()->user_mounted) {
                sh_printf("  (not mounted)\n");
                return;
            }

            if (!shell_ls_user_root()) {
                sh_printf("  (error reading directory)\n");
            }
            return;
        }

        file_t* files = initrd_get_files();
        for(int i=0; i<MAX_FILES; i++) {
            if(files[i].exists && !is_file_protected(files[i].name)) {
    sh_printf(files[i].name);
    sh_printf("\n");
            }
        }
    }
    // 3. CAT (Read File) - Quick hack parsing
    else if (input[0] == 'c' && input[1] == 'a' && input[2] == 't' && input[3] == ' ') {
        char* filename = input + 4; // Skip "cat "

        if (is_file_protected(filename)) {
            sh_printf("Error: Access Denied (Protected File)\n");
            return;
        }

        // If the user asks for /user/<file>, read using PFS.
        if (filename[0] == '/' && filename[1] == 'u' && filename[2] == 's' && filename[3] == 'e' && filename[4] == 'r' && filename[5] == '/') {
            uint8_t* buf = 0;
            uint32_t sz = 0;
            if (pfs_read_user_file(filename, &buf, &sz)) {
                sh_printf("\n");
                for (uint32_t i = 0; i < sz; i++) shell_out_char((char)buf[i]);
                kfree(buf);
                sh_printf("\n");
            } else {
                sh_printf("File not found (or /user not mounted): ");
                sh_printf(filename);
                sh_printf("\n");
            }
        } else {
            file_t* f = initrd_open(filename);
            if (f) {
                sh_printf("\n");
                char* content = (char*)f->address;
                for(uint64_t i=0; i < f->size; i++) {
                    shell_out_char(content[i]);
                }
            } else {
                sh_printf("File not found: ");
                sh_printf(filename);
            }
            sh_printf("\n"); // Newline after content for clean lines
        }
    }
    else if (strcmp(input, "clear") == 0) {
    // Shell is logic-only: clear just emits a couple newlines for now.
    // (A future terminal UI can implement a real clear-screen escape.)
    sh_printf("\n\n");
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
    sh_printf("\n");
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