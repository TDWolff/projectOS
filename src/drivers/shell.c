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
#include "../net/dns.h"
#include "../net/tcp.h"
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

static void sh_print_uint(uint64_t n) {
    if (n == 0) { shell_out_char('0'); return; }
    char buf[21];
    int i = 0;
    while (n) { buf[i++] = '0' + (char)(n % 10); n /= 10; }
    while (i--) shell_out_char(buf[i]);
}

void sh_printf(const char* fmt, ...) {
    if (!fmt) return;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') { shell_out_char(fmt[i]); continue; }
        i++;
        switch (fmt[i]) {
            case 'd': {
                int v = __builtin_va_arg(ap, int);
                if (v < 0) { shell_out_char('-'); sh_print_uint((uint64_t)-(int64_t)v); }
                else sh_print_uint((uint64_t)v);
                break;
            }
            case 'u': sh_print_uint((uint64_t)__builtin_va_arg(ap, unsigned int)); break;
            case 's': { const char* s = __builtin_va_arg(ap, const char*); if (s) for (; *s; s++) shell_out_char(*s); break; }
            case 'c': shell_out_char((char)__builtin_va_arg(ap, int)); break;
            case '%': shell_out_char('%'); break;
            default:  shell_out_char('%'); shell_out_char(fmt[i]); break;
        }
    }
    __builtin_va_end(ap);
}

void shell_out_str(const char* s) {
    sh_printf("%s", s);
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

// Returns latency in ms on success, -1 on timeout/error.
// seq_num is 1-based. Re-enables interrupts during waits so get_ticks() advances.
static int32_t shell_ping_once(uint8_t target_ip[4], net_nic_interfaces_t* nic, uint16_t seq_num) {
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
        return -1;
    }

    const uint16_t ident_be    = BSWAP16(0x1CE0);
    const uint16_t seq_be      = BSWAP16(seq_num);
    const uint32_t data_bytes  = 56; // standard ping payload
    const uint32_t payload_len = (uint32_t)sizeof(icmp_echo_hdr_t) + data_bytes;
    const uint32_t ip_len      = (uint32_t)sizeof(ip_packet_t)
                                 + (uint32_t)sizeof(icmp_header_t)
                                 + payload_len;

    ip_packet_t* ip = (ip_packet_t*)kmalloc(ip_len);
    if (!ip) { sh_printf("ping: out of memory\n"); return -1; }
    memset(ip, 0, ip_len);

    ip->protocol               = 1;
    ip->internet_header_length = 5;
    ip->time_to_live           = 64;
    memcpy(ip->destination_protocol_addr, target_ip, 4);

    icmp_header_t* icmp = (icmp_header_t*)ip->data;
    icmp->type     = 8;
    icmp->code     = 0;
    icmp->checksum = 0;

    icmp_echo_hdr_t* echo = (icmp_echo_hdr_t*)icmp->data;
    echo->identifier = ident_be;
    echo->sequence   = seq_be;

    uint8_t* p = (uint8_t*)icmp->data + sizeof(icmp_echo_hdr_t);
    for (uint32_t i = 0; i < data_bytes; i++) p[i] = (uint8_t)(i & 0xFF);

    icmp->checksum = ip_calculate_checksum(
        icmp, (int)(sizeof(icmp_header_t) + payload_len));

    icmp_selftest_reset();
    uint64_t tx_before = nic->tx_packets;
    ip_send(ip, (uint16_t)ip_len, target_ip, dest_mac, nic);
    kfree(ip);

    if (nic->tx_packets == tx_before) {
        sh_printf("ping: seq=%d TX ring full\n", seq_num);
        return -1;
    }

    // Wait up to 4 seconds for reply; interrupts on so timer advances.
    bool got_reply = false;
    __asm__ volatile("sti");
    uint64_t start    = get_ticks();
    uint64_t deadline = start + 400; // 4 seconds at 100 Hz
    while (get_ticks() < deadline) {
        e1000_poll();
        if (icmp_selftest_wait_for_echo_reply(ident_be, seq_be, 1)) {
            got_reply = true;
            break;
        }
        __asm__ volatile("pause");
    }
    uint64_t end = get_ticks();
    __asm__ volatile("cli");

    if (got_reply) {
        uint32_t ms  = (uint32_t)((end - start) * 10); // 100 Hz → 10 ms/tick
        uint8_t  ttl = icmp_selftest_get_ttl();
        sh_printf("%d bytes from ", (int)(sizeof(icmp_header_t) + payload_len));
        sh_print_ip4(target_ip);
        sh_printf(": icmp_seq=%d ttl=%d time=%d ms\n", (int)seq_num, (int)ttl, (int)ms);
        return (int32_t)ms;
    } else {
        sh_printf("Request timeout for icmp_seq=%d\n", (int)seq_num);
        return -1;
    }
}

#define MAX_COMMAND_LEN 128
static char command_buffer[MAX_COMMAND_LEN];
static int buffer_idx = 0;

// Pending command: set by the keyboard ISR (shell_update), consumed by the
// main loop (shell_run_pending_command). Keeps heavy work out of the ISR so
// interrupts (timer, mouse, compositor) stay alive during command execution.
static char g_pending_command[MAX_COMMAND_LEN];
static bool g_command_ready = false;

// Forward declaration — defined later in this file.
void execute_command(char* input);

bool shell_has_pending_command(void) { return g_command_ready; }

void shell_run_pending_command(void) {
    if (!g_command_ready) return;
    g_command_ready = false;
    execute_command(g_pending_command);
    shell_print_prompt();
}

// Entry point for the shell worker task. Runs as a separate scheduled task so
// the main UI loop (compositor, mouse, clock) is never blocked by commands.
void shell_worker_entry(void) {
    while (1) {
        // Always re-enable interrupts. Some commands (e.g. ping) do cli before
        // returning; without this the timer can't preempt and the UI freezes.
        __asm__ volatile("sti");
        if (g_command_ready) {
            shell_run_pending_command();
        } else {
            // Nothing to do: halt until the next interrupt (timer or keyboard).
            // This gives the UI task 100% of CPU time when the shell is idle,
            // instead of burning cycles in a spin loop at 50% CPU share.
            __asm__ volatile("hlt");
        }
    }
}

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
    sh_printf("ls, cat <file>, pfs, netif, netdevice, nettest, ping <ip|host>, nslookup <host>, wget <url>, clear, run <program>\n");
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
        bool resolved_from_name = false;
        if (!parse_ip4(ip_str, target)) {
            // Not a bare IP — try DNS resolution
            net_nic_interfaces_t* nic_tmp = net_get_primary_nic();
            if (!nic_tmp || (nic_tmp->flags & IFF_LOOPBACK)) {
                sh_printf("ping: no real NIC available\n");
                goto ping_done;
            }
            sh_printf("Resolving %s...\n", ip_str);
            if (!dns_resolve(ip_str, target, nic_tmp, 0)) {
                sh_printf("ping: cannot resolve %s\n", ip_str);
                goto ping_done;
            }
            resolved_from_name = true;
        }
        {
            net_nic_interfaces_t* nic = net_get_primary_nic();
            if (!nic || (nic->flags & IFF_LOOPBACK)) {
                sh_printf("ping: no real NIC available\n");
            } else {
                sh_printf("PING %s (", ip_str);
                sh_print_ip4(target);
                sh_printf("): 64 bytes of data.\n");
                (void)resolved_from_name;

                int sent = 0, received = 0;
                uint32_t min_ms = 0xFFFFFFFFu, max_ms = 0, total_ms = 0;

                for (int seq = 1; seq <= 4; seq++) {
                    int32_t ms = shell_ping_once(target, nic, (uint16_t)seq);
                    sent++;
                    if (ms >= 0) {
                        received++;
                        uint32_t ums = (uint32_t)ms;
                        if (ums < min_ms) min_ms = ums;
                        if (ums > max_ms) max_ms = ums;
                        total_ms += ums;
                    }
                    // ~1 second between pings (100 ticks at 100 Hz)
                    if (seq < 4) {
                        __asm__ volatile("sti");
                        uint64_t wait = get_ticks() + 100;
                        while (get_ticks() < wait) { e1000_poll(); __asm__ volatile("pause"); }
                        __asm__ volatile("cli");
                    }
                }

                sh_printf("--- ping statistics ---\n");
                sh_printf("%d packets transmitted, %d received, %d%% packet loss\n",
                          sent, received, sent > 0 ? (sent - received) * 100 / sent : 0);
                if (received > 0) {
                    sh_printf("rtt min/avg/max = %d/%d/%d ms\n",
                              (int)min_ms,
                              (int)(total_ms / (uint32_t)received),
                              (int)max_ms);
                }
            }
        }
        ping_done:;
    }
    else if (input[0]=='n' && input[1]=='s' && input[2]=='l' && input[3]=='o' &&
             input[4]=='o' && input[5]=='k' && input[6]=='u' && input[7]=='p' && input[8]==' ') {
        const char* hostname = input + 9;
        if (!hostname[0]) {
            sh_printf("usage: nslookup <hostname>\n");
        } else {
            net_nic_interfaces_t* nic = net_get_primary_nic();
            if (!nic || (nic->flags & IFF_LOOPBACK)) {
                sh_printf("nslookup: no real NIC available\n");
            } else {
                sh_printf("Resolving ");
                sh_printf(hostname);
                sh_printf("...\n");
                uint8_t resolved[4] = {0};
                if (dns_resolve(hostname, resolved, nic, 0)) {
                    sh_printf("Server: ");
                    uint8_t* srv = nic->dns_server[0] ? nic->dns_server : (uint8_t*)"\x08\x08\x08\x08";
                    sh_print_ip4(srv);
                    sh_printf("\nAddress: ");
                    sh_print_ip4(resolved);
                    sh_printf("\n");
                } else {
                    uint32_t rx  = dns_dbg_rx_count();
                    uint32_t mis = dns_dbg_id_mismatch_count();
                    if (rx == 0) {
                        sh_printf("nslookup: no response received (query may not have left the VM)\n");
                    } else if (mis == rx) {
                        sh_printf("nslookup: got %d response(s) but all had wrong transaction ID\n", (int)rx);
                    } else {
                        sh_printf("nslookup: got %d response(s) but no A record found\n", (int)rx);
                    }
                }
            }
        }
    }
    else if (input[0]=='w' && input[1]=='g' && input[2]=='e' && input[3]=='t' && input[4]==' ') {
        const char* url = input + 5;

        // --- parse http://hostname/path ---
        if (url[0]!='h' || url[1]!='t' || url[2]!='t' || url[3]!='p' ||
            url[4]!=':' || url[5]!='/' || url[6]!='/') {
            sh_printf("wget: only http:// URLs supported\n");
            goto wget_done;
        }
        const char* host_start = url + 7;
        const char* host_end   = host_start;
        while (*host_end && *host_end != '/') host_end++;

        if (host_end == host_start) { sh_printf("wget: empty hostname\n"); goto wget_done; }

        char hostname[128];
        uint32_t hlen = (uint32_t)(host_end - host_start);
        if (hlen >= sizeof(hostname)) { sh_printf("wget: hostname too long\n"); goto wget_done; }
        for (uint32_t i = 0; i < hlen; i++) hostname[i] = host_start[i];
        hostname[hlen] = 0;

        // path (default to "/" if missing)
        char path[256];
        if (*host_end == '/') {
            uint32_t plen = 0;
            const char* p = host_end;
            while (*p && plen < sizeof(path) - 1) path[plen++] = *p++;
            path[plen] = 0;
        } else {
            path[0] = '/'; path[1] = 0;
        }

        net_nic_interfaces_t* nic = net_get_primary_nic();
        if (!nic || (nic->flags & IFF_LOOPBACK)) {
            sh_printf("wget: no real NIC available\n");
            goto wget_done;
        }

        // --- resolve hostname (or parse as IP) ---
        uint8_t remote_ip[4] = {0};
        if (!parse_ip4(hostname, remote_ip)) {
            sh_printf("Resolving %s...\n", hostname);
            if (!dns_resolve(hostname, remote_ip, nic, 0)) {
                sh_printf("wget: cannot resolve %s\n", hostname);
                goto wget_done;
            }
        }

        sh_printf("Connecting to %s (", hostname);
        sh_print_ip4(remote_ip);
        sh_printf("):80...\n");

        // --- TCP connect ---
        tcp_conn_t* conn = tcp_connect(remote_ip, 80, nic);
        if (!conn) { sh_printf("wget: connection refused or timed out\n"); goto wget_done; }
        sh_printf("Connected.\n");

        // --- build HTTP/1.0 GET request ---
        char req[512];
        int rlen = 0;
        const char* s;
        for (s = "GET ";              *s && rlen<511; s++) req[rlen++] = *s;
        for (s = path;                *s && rlen<511; s++) req[rlen++] = *s;
        for (s = " HTTP/1.0\r\nHost: "; *s && rlen<511; s++) req[rlen++] = *s;
        for (s = hostname;            *s && rlen<511; s++) req[rlen++] = *s;
        for (s = "\r\nConnection: close\r\nUser-Agent: ProjectOS/1.0\r\n\r\n";
             *s && rlen<511; s++) req[rlen++] = *s;
        req[rlen] = 0;

        tcp_send(conn, (uint8_t*)req, (uint32_t)rlen);

        // --- stream response to terminal ---
        sh_printf("---\n");
        uint32_t total   = 0;
        uint8_t  rbuf[256];

        __asm__ volatile("sti");
        uint64_t deadline   = get_ticks() + 1000; // 10s hard limit
        uint64_t idle_since = get_ticks();

        while (1) {
            e1000_poll();

            uint32_t n = tcp_recv(conn, rbuf, sizeof(rbuf));
            if (n > 0) {
                for (uint32_t i = 0; i < n; i++) sh_putc((char)rbuf[i]);
                total     += n;
                idle_since = get_ticks();
                deadline   = get_ticks() + 1000; // reset hard limit on activity
            }

            if (tcp_is_done(conn)) break;

            uint64_t now = get_ticks();
            if (now >= deadline) break;
            if (now - idle_since > 300) break; // 3s idle = server done sending
            __asm__ volatile("pause");
        }
        __asm__ volatile("cli");

        sh_printf("\n---\n%d bytes received.\n", (int)total);

        tcp_close(conn);
        tcp_free(conn);
        wget_done:;
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
        // Don't run the command here — we're inside the keyboard ISR with
        // interrupts disabled (interrupt gate 0x8E). Queue it for the main
        // loop so timer/mouse/compositor keep running during execution.
        if (!g_command_ready) {
            memcpy(g_pending_command, command_buffer, MAX_COMMAND_LEN);
            g_command_ready = true;
        }
        memset(command_buffer, 0, MAX_COMMAND_LEN);
        buffer_idx = 0;
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