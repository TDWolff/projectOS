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
#include "../net/tls.h"
#include "../drivers/net/e1000.h"
#include "timer.h"
#include "editor.h"

// Ctrl+C interrupt flag. Set by the keyboard ISR, polled by long-running
// commands. Volatile prevents the compiler caching it in a register.
static volatile bool g_shell_interrupted = false;
static volatile bool g_command_running   = false;

// Sudo elevation flag. When true, is_file_protected() and the read-only
// check in rm are bypassed for the duration of the elevated command.
static volatile bool g_sudo_active = false;

// Password input state (written by keyboard ISR, read by shell worker).
static volatile bool g_pw_ready     = false;
static volatile bool g_pw_cancelled = false;
static char g_pw_buf[64];
static int  g_pw_len = 0;

// Shell output sink: lets the windowed terminal display shell I/O.
static void (*g_shell_putc)(char c, void* user) = 0;
static void* g_shell_putc_user = 0;

// Raw keyboard hook: when set, keyboard chars bypass the shell command buffer.
static void (*g_input_handler)(char c, void* user) = 0;
static void* g_input_handler_user = 0;

// Terminal grid dimensions (populated by dock when it creates the terminal window).
static int g_term_rows = 24;
static int g_term_cols = 80;

// Current working directory. Only "/" (initrd, read-only) and "/user" (FAT32)
// are valid. Defaults to /user since that's the only writable location.
static char g_cwd[128] = "/user";

const char* shell_get_cwd(void) { return g_cwd; }

// Normalize an absolute path in-place: collapse . and .. components.
static void path_normalize(char* path) {
    // Component stack stored as a single flat buffer; we track segment starts.
    char buf[128];
    int segs[16];   // start index of each segment in buf (after its leading /)
    int nseg = 0;
    int bi   = 0;

    int i = 1; // skip leading /
    while (path[i]) {
        // skip consecutive slashes
        while (path[i] == '/') i++;
        if (!path[i]) break;
        // read component
        int start = i;
        while (path[i] && path[i] != '/') i++;
        int len = i - start;
        if (len == 1 && path[start] == '.') {
            continue; // . = current dir, skip
        } else if (len == 2 && path[start] == '.' && path[start+1] == '.') {
            if (nseg > 0) { bi = segs[--nseg]; } // pop last segment
        } else {
            if (bi < 126 - len) {
                segs[nseg < 16 ? nseg++ : nseg-1] = bi;
                buf[bi++] = '/';
                for (int j = 0; j < len; j++) buf[bi++] = path[start + j];
            }
        }
    }
    buf[bi] = 0;
    if (bi == 0) { path[0] = '/'; path[1] = 0; return; }
    int j = 0; while (buf[j]) { path[j] = buf[j]; j++; } path[j] = 0;
}

// Resolve a user-supplied path against the CWD, then normalize . and ..
static void shell_resolve_path(const char* in, char* out, int outsz) {
    if (in[0] == '/') {
        int i = 0;
        while (in[i] && i < outsz - 1) { out[i] = in[i]; i++; }
        out[i] = 0;
    } else {
        int ci = 0;
        while (g_cwd[ci] && ci < outsz - 1) { out[ci] = g_cwd[ci]; ci++; }
        if (ci < outsz - 1 && (ci == 0 || out[ci-1] != '/')) out[ci++] = '/';
        int ii = 0;
        while (in[ii] && ci < outsz - 1) { out[ci++] = in[ii++]; }
        out[ci] = 0;
    }
    path_normalize(out);
    // Remove trailing slash unless root
    int len = 0; while (out[len]) len++;
    if (len > 1 && out[len-1] == '/') out[len-1] = 0;
}

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

void shell_set_input_handler(void (*handler)(char c, void* user), void* user) {
    g_input_handler      = handler;
    g_input_handler_user = user;
}

void shell_set_term_size(int rows, int cols) {
    if (rows > 0) g_term_rows = rows;
    if (cols > 0) g_term_cols = cols;
}

void shell_get_term_size(int* rows, int* cols) {
    if (rows) *rows = g_term_rows;
    if (cols) *cols = g_term_cols;
}

void shell_out_str(const char* s) {
    sh_printf("%s", s);
}

void shell_print_prompt() {
    const char* username = settings_get("username");
    if (!username || !username[0]) username = "User";
    sh_printf(username);
    sh_printf(" ");
    sh_printf(g_cwd);
    sh_printf(" % ");
}

// List of files to exclude from user view/access
static const char* protected_files[] = {
    "settings.pset",
    "sudopass",      // sudo password — never visible or removable
    "kernel.bin",
    "limine.conf",
    "terminal.dock",
    0
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

// --- ls flags and listing helpers ---

typedef struct {
    int  count;
    bool show_all;  // -a: show protected files
    bool long_fmt;  // -l: show size column
    bool human;     // -h: human-readable sizes (implies -l)
} ls_ctx_t;

// Print s left-justified in a field of `width` chars.
static void sh_print_field(const char* s, int width) {
    int i = 0;
    for (; s[i] && i < width; i++) sh_putc(s[i]);
    for (; i < width; i++) sh_putc(' ');
}

// Print uint32 right-justified in a field of `width` chars.
static void sh_print_uint_field(uint32_t v, int width) {
    char buf[12];
    int len = 0;
    if (v == 0) { buf[len++] = '0'; }
    else { uint32_t tmp = v; while (tmp) { buf[len++] = (char)('0' + tmp % 10); tmp /= 10; } }
    // buf is reversed; pad first, then emit in reverse
    int pad = width - len;
    while (pad-- > 0) sh_putc(' ');
    while (len-- > 0) sh_putc(buf[len]);
}

// Human-readable size: "1023 B", "12 K", "3 M"
static void sh_print_size_human(uint32_t sz) {
    if (sz >= 1024 * 1024) {
        sh_print_uint_field(sz / (1024 * 1024), 4);
        sh_printf(" M");
    } else if (sz >= 1024) {
        sh_print_uint_field(sz / 1024, 4);
        sh_printf(" K");
    } else {
        sh_print_uint_field(sz, 4);
        sh_printf(" B");
    }
}

static bool shell_pfs_ls_info_cb(const char* name, bool is_dir, uint32_t size, void* user) {
    ls_ctx_t* ctx = (ls_ctx_t*)user;
    if (!name || !name[0]) return true;
    if (!ctx->show_all && is_file_protected(name)) return true;

    sh_printf("  ");
    if (ctx->long_fmt || ctx->human) {
        char namebuf[28];
        int ni = 0;
        while (name[ni] && ni < 23) { namebuf[ni] = name[ni]; ni++; }
        if (is_dir && ni < 24) namebuf[ni++] = '/';
        namebuf[ni] = 0;
        sh_print_field(namebuf, 24);
        sh_printf("  ");
        if (is_dir) {
            sh_printf("        -");
        } else if (ctx->human) {
            sh_print_size_human(size);
        } else {
            sh_print_uint_field(size, 9);
        }
        // FAT attribute column: R=read-only H=hidden A=archive
        char path[160]; int pi = 0;
        path[pi++]='/'; path[pi++]='u'; path[pi++]='s'; path[pi++]='e'; path[pi++]='r'; path[pi++]='/';
        for (int i = 0; name[i] && pi < 158; i++) path[pi++] = name[i];
        path[pi] = 0;
        uint8_t attr = 0;
        pfs_get_user_file_attr(path, &attr);
        char aflags[4] = "---";
        if (attr & 0x01) aflags[0] = 'R';
        if (attr & 0x02) aflags[1] = 'H';
        if (attr & 0x20) aflags[2] = 'A';
        sh_printf("  ");
        sh_printf(aflags);
    } else {
        sh_printf(name);
        if (is_dir) sh_printf("/");
    }
    sh_printf("\n");
    ctx->count++;
    return true;
}

// Adapter from long-name callback (no size) to info callback
static bool shell_pfs_ls_long_cb(const char* name, bool is_dir, void* user) {
    return shell_pfs_ls_info_cb(name, is_dir, 0, user);
}

static bool shell_ls_user_root(ls_ctx_t* ctx) {
    ctx->count = 0;
    // Use info callback (with size) when long format is requested, plain otherwise.
    bool ok;
    if (ctx->long_fmt || ctx->human) {
        ok = pfs_list_user_root_info(shell_pfs_ls_info_cb, ctx);
    } else {
        ok = pfs_list_user_root_long(shell_pfs_ls_long_cb, ctx);
    }
    if (!ok) return false;
    if (ctx->count == 0) sh_printf("  (empty)\n");
    return true;
}

// Parse flags and optional path from everything after "ls" (may start with space or be empty).
static void ls_parse_args(const char* arg, ls_ctx_t* ctx, char* path_out, int path_sz) {
    path_out[0] = 0;
    while (*arg == ' ') arg++;
    while (*arg) {
        while (*arg == ' ') arg++;
        if (!*arg) break;
        if (*arg == '-') {
            arg++;
            while (*arg && *arg != ' ') {
                if (*arg == 'a') ctx->show_all = true;
                else if (*arg == 'l') ctx->long_fmt = true;
                else if (*arg == 'h') { ctx->human = true; ctx->long_fmt = true; }
                arg++;
            }
        } else {
            // path token (take first one)
            int i = 0;
            while (*arg && *arg != ' ' && i < path_sz - 1) { path_out[i++] = *arg++; }
            path_out[i] = 0;
            while (*arg && *arg != ' ') arg++;
        }
    }
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
        uint64_t deadline = get_ticks() + 2000; // 2 seconds at 1000 Hz
        while (get_ticks() < deadline) {
            if (g_shell_interrupted) { __asm__ volatile("cli"); return -1; }
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
    uint64_t deadline = start + 4000; // 4 seconds at 1000 Hz
    while (get_ticks() < deadline) {
        if (g_shell_interrupted) break;
        e1000_poll();
        if (icmp_selftest_wait_for_echo_reply(ident_be, seq_be, 1)) {
            got_reply = true;
            break;
        }
        __asm__ volatile("pause");
    }
    uint64_t end = get_ticks();
    __asm__ volatile("cli");
    if (g_shell_interrupted) return -1;

    if (got_reply) {
        uint32_t ms  = (uint32_t)(end - start); // 1000 Hz → 1 ms/tick
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
static int buffer_len = 0;  // total chars in command_buffer
static int cursor_pos = 0;  // cursor position (0..buffer_len)

static void sh_emit_csi(int n, char cmd) {
    if (n <= 0) return;
    shell_out_char('\x1B'); shell_out_char('[');
    char buf[8]; int i = 0, tmp = n;
    while (tmp) { buf[i++] = (char)('0' + tmp % 10); tmp /= 10; }
    while (i > 0) shell_out_char(buf[--i]);
    shell_out_char(cmd);
}

// Pending command: set by the keyboard ISR (shell_update), consumed by the
// main loop (shell_run_pending_command). Keeps heavy work out of the ISR so
// interrupts (timer, mouse, compositor) stay alive during command execution.
static char g_pending_command[MAX_COMMAND_LEN];
static bool g_command_ready = false;

// --- Command history ---
#define HISTORY_MAX 32
static char g_history[HISTORY_MAX][MAX_COMMAND_LEN];
static int  g_history_count = 0;
static int  g_history_head  = 0;
static int  g_history_pos   = -1;   // -1 = not browsing
static char g_history_saved[MAX_COMMAND_LEN]; // saved buffer on first Up press

static void history_push(const char* cmd) {
    if (!cmd || !cmd[0]) return;
    if (g_history_count > 0) {
        int last = (g_history_head - 1 + HISTORY_MAX) % HISTORY_MAX;
        if (strcmp(g_history[last], cmd) == 0) return; // no duplicate
    }
    int i = 0;
    while (cmd[i] && i < MAX_COMMAND_LEN - 1) { g_history[g_history_head][i] = cmd[i]; i++; }
    g_history[g_history_head][i] = 0;
    g_history_head = (g_history_head + 1) % HISTORY_MAX;
    if (g_history_count < HISTORY_MAX) g_history_count++;
}

static const char* history_get(int offset) {
    if (offset < 0 || offset >= g_history_count) return 0;
    int idx = (g_history_head - 1 - offset + HISTORY_MAX * 2) % HISTORY_MAX;
    return g_history[idx];
}

bool shell_is_interrupted(void) {
    if (!g_shell_interrupted) return false;
    g_shell_interrupted = false;
    return true;
}

// --- sudo password input ---

static void sudo_pw_handler(char c, void* user) {
    (void)user;
    if (c == '\n') {
        g_pw_buf[g_pw_len] = 0;
        g_pw_ready = true;
        shell_set_input_handler(0, 0);
        return;
    }
    if (c == '\x03' || c == '\x1B') {
        g_pw_cancelled = true;
        shell_set_input_handler(0, 0);
        return;
    }
    if (c == '\b') {
        if (g_pw_len > 0) g_pw_len--;
        return;
    }
    if (g_pw_len < 63) g_pw_buf[g_pw_len++] = c;
}

// Print prompt, then block (sti+pause) until the ISR delivers a password or cancel.
// Returns true if a password was entered; false if cancelled.
static bool sudo_read_password(const char* prompt) {
    g_pw_ready     = false;
    g_pw_cancelled = false;
    g_pw_len       = 0;
    memset(g_pw_buf, 0, sizeof(g_pw_buf));
    sh_printf(prompt);
    shell_set_input_handler(sudo_pw_handler, 0);
    __asm__ volatile("sti");
    while (!g_pw_ready && !g_pw_cancelled) __asm__ volatile("hlt");
    sh_printf("\n");
    return (bool)g_pw_ready;
}

// Forward declaration — defined later in this file.
void execute_command(char* input);

bool shell_has_pending_command(void) { return g_command_ready; }

void shell_run_pending_command(void) {
    if (!g_command_ready) return;
    g_command_ready      = false;
    g_shell_interrupted  = false;
    g_command_running    = true;
    execute_command(g_pending_command);
    g_command_running    = false;
    g_shell_interrupted  = false;
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
    buffer_len = 0;
    cursor_pos = 0;

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
    sh_printf("ls, cat <file>, rm <file>, mkdir <dir>, chmod MODE <file>, sudo <cmd>, pfs,\n");
    sh_printf("netif, netdevice, nettest, ping <ip|host>, nslookup <host>, wget <url>,\n");
    sh_printf("clear, run <program>, nano <file>, vim <file>\n");
    sh_printf("  - sudo passwd       : set or change the sudo password\n");
    sh_printf("  - sudo <cmd>        : run <cmd> with elevated privileges\n");
    sh_printf("  - chmod MODE file   : R=read-only H=hidden A=archive\n");
    sh_printf("  - pfs               : shows /user mount status\n");
    sh_printf("  - ping <ip|host>    : send ICMP echo\n");
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
                    if (g_shell_interrupted) goto ping_done;
                    int32_t ms = shell_ping_once(target, nic, (uint16_t)seq);
                    if (g_shell_interrupted) goto ping_done;
                    sent++;
                    if (ms >= 0) {
                        received++;
                        uint32_t ums = (uint32_t)ms;
                        if (ums < min_ms) min_ms = ums;
                        if (ums > max_ms) max_ms = ums;
                        total_ms += ums;
                    }
                    // ~1 second between pings (1000 ticks at 1000 Hz)
                    if (seq < 4) {
                        __asm__ volatile("sti");
                        uint64_t wait = get_ticks() + 1000;
                        while (get_ticks() < wait) {
                            if (g_shell_interrupted) { __asm__ volatile("cli"); goto ping_done; }
                            e1000_poll();
                            __asm__ volatile("pause");
                        }
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

        // --- detect scheme ---
        bool use_tls = false;
        const char* after_scheme;
        // check https://
        if (url[0]=='h' && url[1]=='t' && url[2]=='t' && url[3]=='p' &&
            url[4]=='s' && url[5]==':' && url[6]=='/' && url[7]=='/') {
            use_tls = true;
            after_scheme = url + 8;
        } else if (url[0]=='h' && url[1]=='t' && url[2]=='t' && url[3]=='p' &&
                   url[4]==':' && url[5]=='/' && url[6]=='/') {
            use_tls = false;
            after_scheme = url + 7;
        } else {
            sh_printf("wget: only http:// and https:// URLs are supported\n");
            goto wget_done;
        }

        const char* host_start = after_scheme;
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

        // --- resolve hostname ---
        uint8_t remote_ip[4] = {0};
        if (!parse_ip4(hostname, remote_ip)) {
            sh_printf("Resolving %s...\n", hostname);
            if (!dns_resolve(hostname, remote_ip, nic, 0)) {
                sh_printf("wget: cannot resolve %s\n", hostname);
                goto wget_done;
            }
        }

        uint16_t port = use_tls ? 443 : 80;
        sh_printf("Connecting to %s (", hostname);
        sh_print_ip4(remote_ip);
        sh_printf("):%d%s...\n", (int)port, use_tls ? " [TLS]" : "");

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

        uint32_t total = 0;
        uint8_t  rbuf[256];
        __asm__ volatile("sti");

        if (use_tls) {
            // --- HTTPS path ---
            tls_conn_t* tconn = tls_connect(hostname, remote_ip, port, nic);
            if (!tconn) { sh_printf("wget: TLS handshake failed\n"); __asm__ volatile("cli"); goto wget_done; }
            sh_printf("Connected (TLS).\n");

            tls_send(tconn, (uint8_t*)req, (uint32_t)rlen);

            sh_printf("---\n");
            uint64_t deadline   = get_ticks() + 15000;
            uint64_t idle_since = get_ticks();

            while (1) {
                int n = tls_recv(tconn, rbuf, sizeof(rbuf));
                if (n > 0) {
                    for (int i = 0; i < n; i++) sh_putc((char)rbuf[i]);
                    total     += (uint32_t)n;
                    idle_since = get_ticks();
                    deadline   = get_ticks() + 15000;
                } else if (n == 0) {
                    break; // clean close
                } else {
                    break; // error
                }

                if (shell_is_interrupted()) { sh_printf("\nInterrupted.\n"); break; }
                uint64_t now = get_ticks();
                if (now >= deadline) break;
                if (now - idle_since > 5000) break;
                __asm__ volatile("pause");
            }

            tls_close(tconn);
        } else {
            // --- HTTP path ---
            tcp_conn_t* conn = tcp_connect(remote_ip, port, nic);
            if (!conn) { sh_printf("wget: connection refused or timed out\n"); __asm__ volatile("cli"); goto wget_done; }
            sh_printf("Connected.\n");

            tcp_send(conn, (uint8_t*)req, (uint32_t)rlen);

            sh_printf("---\n");
            uint64_t deadline   = get_ticks() + 10000;
            uint64_t idle_since = get_ticks();

            while (1) {
                e1000_poll();
                uint32_t n = tcp_recv(conn, rbuf, sizeof(rbuf));
                if (n > 0) {
                    for (uint32_t i = 0; i < n; i++) sh_putc((char)rbuf[i]);
                    total     += n;
                    idle_since = get_ticks();
                    deadline   = get_ticks() + 10000;
                }
                if (tcp_is_done(conn)) break;
                if (shell_is_interrupted()) { sh_printf("\nInterrupted.\n"); break; }
                uint64_t now = get_ticks();
                if (now >= deadline) break;
                if (now - idle_since > 3000) break;
                __asm__ volatile("pause");
            }

            tcp_close(conn);
            tcp_free(conn);
        }

        __asm__ volatile("cli");
        sh_printf("\n---\n%d bytes received.\n", (int)total);
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
             (input[0]=='l' && input[1]=='s' && (input[2]==0 || input[2]==' '))) {
        ls_ctx_t ctx = { 0, false, false, false };
        char path_arg[128];
        path_arg[0] = 0;

        // Parse flags and optional path from everything after "ls"
        ls_parse_args(input + 2, &ctx, path_arg, (int)sizeof(path_arg));

        // Determine target directory
        char target[128];
        if (path_arg[0]) {
            shell_resolve_path(path_arg, target, (int)sizeof(target));
        } else {
            int ti = 0;
            while (g_cwd[ti] && ti < (int)sizeof(target)-1) { target[ti] = g_cwd[ti]; ti++; }
            target[ti] = 0;
        }

        // Normalise trailing slash
        int tlen = 0; while (target[tlen]) tlen++;
        if (tlen > 1 && target[tlen-1] == '/') { target[tlen-1] = 0; tlen--; }

        bool is_user = (target[0]=='/' && target[1]=='u' && target[2]=='s' &&
                        target[3]=='e' && target[4]=='r' && target[5]==0);
        bool is_root = (target[0]=='/' && target[1]==0);

        if (is_user) {
            if (!pfs_get_state() || !pfs_get_state()->user_mounted) {
                sh_printf("  (not mounted)\n");
            } else if (!shell_ls_user_root(&ctx)) {
                sh_printf("  (error reading directory)\n");
            }
        } else if (is_root) {
            file_t* files = initrd_get_files();
            int count = 0;
            for (int i = 0; i < MAX_FILES; i++) {
                if (!files[i].exists) continue;
                if (!ctx.show_all && is_file_protected(files[i].name)) continue;
                if (ctx.long_fmt || ctx.human) {
                    sh_printf("  ");
                    sh_print_field(files[i].name, 24);
                    sh_printf("  ");
                    if (ctx.human) {
                        sh_print_size_human((uint32_t)files[i].size);
                    } else {
                        sh_print_uint_field((uint32_t)files[i].size, 9);
                    }
                    sh_printf("\n");
                } else {
                    sh_printf(files[i].name);
                    sh_printf("\n");
                }
                count++;
            }
            if (count == 0) sh_printf("  (empty)\n");
        } else {
            sh_printf("ls: no such directory: ");
            sh_printf(target);
            sh_printf("\n");
        }
    }
    // 3. CAT (Read File)
    else if (input[0] == 'c' && input[1] == 'a' && input[2] == 't' && input[3] == ' ') {
        const char* arg = input + 4;

        if (is_file_protected(arg)) {
            sh_printf("Error: Access Denied (Protected File)\n");
            return;
        }

        char resolved[160];
        shell_resolve_path(arg, resolved, (int)sizeof(resolved));

        // /user/<file>  → PFS read
        bool in_user = (resolved[0]=='/' && resolved[1]=='u' && resolved[2]=='s' &&
                        resolved[3]=='e' && resolved[4]=='r' && resolved[5]=='/');
        if (in_user) {
            uint8_t* buf = 0; uint32_t sz = 0;
            if (pfs_read_user_file(resolved, &buf, &sz)) {
                sh_printf("\n");
                for (uint32_t i = 0; i < sz; i++) shell_out_char((char)buf[i]);
                kfree(buf);
                sh_printf("\n");
            } else {
                sh_printf("File not found: "); sh_printf(resolved); sh_printf("\n");
            }
        } else {
            // initrd — try the bare filename portion
            const char* bare = resolved;
            for (int i = 0; resolved[i]; i++) if (resolved[i] == '/') bare = resolved + i + 1;
            file_t* f = initrd_open(bare);
            if (f) {
                sh_printf("\n");
                char* content = (char*)f->address;
                for (uint64_t i = 0; i < f->size; i++) shell_out_char(content[i]);
                sh_printf("\n");
            } else {
                sh_printf("File not found: "); sh_printf(resolved); sh_printf("\n");
            }
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
        // // kprintf("\nSystem ticks: %d", get_ticks());
    }
    else if (strcmp(input, "divzero") == 0) {
        // // kprintf("\nDividing by zero...");
        volatile int a = 1;
        volatile int b = 0;
        volatile int c = a / b;
        (void)c;
    }
    else if (input[0] == 'e' && input[1] == 'c' && input[2] == 'h' && input[3] == 'o') {
        // // kprintf("\n%s", input + 5);
    }
    else if (input[0]=='n' && input[1]=='a' && input[2]=='n' && input[3]=='o' && input[4]==' ') {
        char resolved[160];
        shell_resolve_path(input + 5, resolved, (int)sizeof(resolved));
        if (!resolved[0]) { sh_printf("usage: nano <filename>\n"); }
        else { editor_open_nano(resolved); }
    }
    else if (strcmp(input, "nano") == 0) {
        sh_printf("usage: nano <filename>\n");
    }
    else if (input[0]=='v' && input[1]=='i' && input[2]=='m' && input[3]==' ') {
        char resolved[160];
        shell_resolve_path(input + 4, resolved, (int)sizeof(resolved));
        if (!resolved[0]) { sh_printf("usage: vim <filename>\n"); }
        else { editor_open_vim(resolved); }
    }
    else if (strcmp(input, "vim") == 0) {
        sh_printf("usage: vim <filename>\n");
    }
    else if (input[0]=='c' && input[1]=='d' &&
             (input[2]==0 || input[2]==' ' || input[2]=='.' || input[2]=='/')) {
        // Extract argument: "cd" → home, "cd <arg>", "cd.." and "cd/path" also accepted
        const char* arg = 0;
        if      (input[2] == ' ') arg = input + 3;
        else if (input[2] != 0)   arg = input + 2; // cd.. or cd/foo
        // else arg stays 0 → cd with no arg

        if (!arg || !arg[0]) {
            // cd alone → home (/user)
            g_cwd[0]='/'; g_cwd[1]='u'; g_cwd[2]='s';
            g_cwd[3]='e'; g_cwd[4]='r'; g_cwd[5]=0;
        } else {
            char resolved[128];
            shell_resolve_path(arg, resolved, (int)sizeof(resolved));
            bool ok_root = (resolved[0]=='/' && resolved[1]==0);
            bool ok_user = (resolved[0]=='/' && resolved[1]=='u' && resolved[2]=='s' &&
                            resolved[3]=='e' && resolved[4]=='r' && resolved[5]==0);
            if (ok_root || ok_user) {
                int i = 0;
                while (resolved[i] && i < (int)sizeof(g_cwd)-1) { g_cwd[i] = resolved[i]; i++; }
                g_cwd[i] = 0;
            } else {
                sh_printf("cd: no such directory: "); sh_printf(resolved); sh_printf("\n");
            }
        }
    }
    else if (input[0]=='r' && input[1]=='m' && input[2]==' ') {
        const char* arg = input + 3;
        while (*arg == ' ') arg++;
        if (!arg[0]) {
            sh_printf("usage: rm <filename>\n");
        } else if (is_file_protected(arg)) {
            sh_printf("rm: permission denied: %s\n", arg);
        } else {
            char resolved[160];
            shell_resolve_path(arg, resolved, (int)sizeof(resolved));

            bool in_user = (resolved[0]=='/' && resolved[1]=='u' && resolved[2]=='s' &&
                            resolved[3]=='e' && resolved[4]=='r' && resolved[5]=='/');
            if (!in_user) {
                sh_printf("rm: can only delete files in /user\n");
            } else {
                // Refuse to delete read-only files unless sudo is active
                uint8_t attr = 0;
                pfs_get_user_file_attr(resolved, &attr);
                if ((attr & 0x01) && !g_sudo_active) {
                    sh_printf("rm: cannot remove '%s': read-only file (use chmod +w or sudo rm)\n", arg);
                } else if (pfs_delete_user_file(resolved)) {
                    sh_printf("removed '%s'\n", resolved + 6);
                } else {
                    sh_printf("rm: cannot remove '%s': no such file\n", arg);
                }
            }
        }
    }
    else if (input[0]=='m' && input[1]=='k' && input[2]=='d' && input[3]=='i' && input[4]=='r' && input[5]==' ') {
        const char* arg = input + 6;
        while (*arg == ' ') arg++;
        if (!arg[0]) {
            sh_printf("usage: mkdir <dirname>\n");
        } else {
            char resolved[160];
            shell_resolve_path(arg, resolved, (int)sizeof(resolved));

            bool in_user = (resolved[0]=='/' && resolved[1]=='u' && resolved[2]=='s' &&
                            resolved[3]=='e' && resolved[4]=='r' && resolved[5]=='/');
            if (!in_user) {
                sh_printf("mkdir: can only create directories in /user\n");
            } else {
                if (pfs_mkdir_user(resolved)) {
                    sh_printf("created directory '%s'\n", resolved + 6);
                } else {
                    sh_printf("mkdir: cannot create directory '%s': already exists or no space\n", arg);
                }
            }
        }
    }
    else if (input[0]=='c' && input[1]=='h' && input[2]=='m' && input[3]=='o' && input[4]=='d' && input[5]==' ') {
        const char* arg = input + 6;
        while (*arg == ' ') arg++;

        // Split into MODE and FILE tokens
        char mode[32]; int mi = 0;
        while (*arg && *arg != ' ' && mi < 31) mode[mi++] = *arg++;
        mode[mi] = 0;
        while (*arg == ' ') arg++;
        const char* filename = arg;

        if (!mode[0] || !filename[0]) {
            sh_printf("usage: chmod MODE FILE\n");
            sh_printf("  Octal:    chmod 444 file  (read-only)  chmod 644 file  (read-write)\n");
            sh_printf("  Symbolic: [ugoa][+-=][rwxhHa]\n");
            sh_printf("  Bits:     R=read-only  H=hidden  A=archive\n");
            sh_printf("  Example:  chmod -w file  chmod +h file  chmod a+w file\n");
        } else if (is_file_protected(filename)) {
            sh_printf("chmod: permission denied: %s\n", filename);
        } else {
            char resolved[160];
            shell_resolve_path(filename, resolved, (int)sizeof(resolved));
            bool in_user = (resolved[0]=='/' && resolved[1]=='u' && resolved[2]=='s' &&
                            resolved[3]=='e' && resolved[4]=='r' && resolved[5]=='/');
            if (!in_user) {
                sh_printf("chmod: only /user files are supported\n");
            } else {
                uint8_t attr = 0;
                if (!pfs_get_user_file_attr(resolved, &attr)) {
                    sh_printf("chmod: %s: no such file\n", filename);
                } else {
                    bool ok = true;
                    // --- Octal mode ---
                    if (mode[0] >= '0' && mode[0] <= '7') {
                        int oct = 0;
                        for (int i = 0; mode[i] && ok; i++) {
                            if (mode[i] < '0' || mode[i] > '7') { ok = false; break; }
                            oct = oct * 8 + (mode[i] - '0');
                        }
                        if (ok) {
                            if (oct & 0222) attr &= ~0x01u; // any write bit → clear read-only
                            else            attr |=  0x01u; // no write bits  → set read-only
                        }
                    }
                    // --- Symbolic mode: [ugoa]*[+-=][rwxhHa]+ ---
                    else {
                        const char* m = mode;
                        // Skip who-specifier (we map everything to the same FAT bits)
                        while (*m=='u'||*m=='g'||*m=='o'||*m=='a') m++;
                        if (*m != '+' && *m != '-' && *m != '=') {
                            sh_printf("chmod: invalid mode: %s\n", mode);
                            ok = false;
                        }
                        if (ok) {
                            char op = *m++;
                            if (!*m) { sh_printf("chmod: missing permissions after '%c'\n", op); ok = false; }
                            if (ok) {
                                if (op == '=') {
                                    // =r or =r → read-only; =rw/=w → read-write; clear hidden/archive
                                    bool has_w = false;
                                    for (int i = 0; m[i]; i++) if (m[i]=='w') { has_w=true; break; }
                                    attr = has_w ? (attr & ~0x01u) : (attr | 0x01u);
                                    attr &= ~0x02u; // clear hidden
                                    attr &= ~0x20u; // clear archive
                                } else {
                                    bool add = (op == '+');
                                    for (int i = 0; m[i]; i++) {
                                        switch (m[i]) {
                                        case 'w':
                                            if (add) attr &= ~0x01u; // +w = clear read-only
                                            else     attr |=  0x01u; // -w = set read-only
                                            break;
                                        case 'r': case 'x':
                                            break; // read/execute have no FAT equivalent; no-op
                                        case 'h': case 'H':
                                            if (add) attr |=  0x02u;
                                            else     attr &= ~0x02u;
                                            break;
                                        case 'a':
                                            if (add) attr |=  0x20u;
                                            else     attr &= ~0x20u;
                                            break;
                                        default:
                                            sh_printf("chmod: unknown permission bit '%c'\n", m[i]);
                                            ok = false;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (ok) {
                        if (pfs_set_user_file_attr(resolved, attr)) {
                            // Show new attribute state
                            char aflags[4] = "---";
                            if (attr & 0x01) aflags[0] = 'R';
                            if (attr & 0x02) aflags[1] = 'H';
                            if (attr & 0x20) aflags[2] = 'A';
                            sh_printf("%s: [%s]\n", resolved + 6, aflags);
                        } else {
                            sh_printf("chmod: failed to write attributes\n");
                        }
                    }
                }
            }
        }
    }
    else if (input[0]=='s' && input[1]=='u' && input[2]=='d' && input[3]=='o' &&
             (input[4]==' ' || input[4]==0)) {
        const char* subcmd = (input[4]==' ') ? input + 5 : "";
        while (*subcmd == ' ') subcmd++;

        // sudo passwd — set or change the sudo password
        if (subcmd[0]=='p' && subcmd[1]=='a' && subcmd[2]=='s' && subcmd[3]=='s' &&
            subcmd[4]=='w' && subcmd[5]=='d' && subcmd[6]==0) {

            uint8_t* existing = 0; uint32_t exsz = 0;
            bool has_pw = pfs_read_user_file("/user/sudopass", &existing, &exsz);

            if (has_pw && existing && exsz > 0) {
                if (!sudo_read_password("Current sudo password: ")) {
                    sh_printf("passwd: cancelled\n");
                    kfree(existing);
                    return;
                }
                bool match = ((int)exsz == g_pw_len);
                for (int i = 0; match && i < g_pw_len; i++)
                    if (g_pw_buf[i] != (char)existing[i]) match = false;
                kfree(existing); existing = 0;
                if (!match) { sh_printf("passwd: incorrect password\n"); return; }
            } else {
                if (existing) { kfree(existing); existing = 0; }
            }

            if (!sudo_read_password("New sudo password: ")) {
                sh_printf("passwd: cancelled\n"); return;
            }
            char new_pw[64]; int new_len = g_pw_len;
            memcpy(new_pw, g_pw_buf, (uint32_t)(new_len < 64 ? new_len : 63));

            if (!sudo_read_password("Confirm new sudo password: ")) {
                sh_printf("passwd: cancelled\n"); return;
            }
            if (new_len != g_pw_len) { sh_printf("passwd: passwords do not match\n"); return; }
            bool m2 = true;
            for (int i = 0; m2 && i < new_len; i++) if (new_pw[i] != g_pw_buf[i]) m2 = false;
            if (!m2) { sh_printf("passwd: passwords do not match\n"); return; }
            if (new_len == 0) { sh_printf("passwd: password cannot be empty\n"); return; }

            if (pfs_write_user_file("/user/sudopass", (uint8_t*)new_pw, (uint32_t)new_len)) {
                pfs_set_user_file_attr("/user/sudopass", 0x03); // read-only + hidden
                sh_printf("passwd: password updated\n");
            } else {
                sh_printf("passwd: failed to save password\n");
            }
        }
        // sudo <command> — authenticate and run subcmd with elevated privileges
        else if (subcmd[0]) {
            if (!pfs_get_state() || !pfs_get_state()->user_mounted) {
                sh_printf("sudo: /user not mounted, cannot verify password\n");
                return;
            }
            uint8_t* stored = 0; uint32_t storedsz = 0;
            if (!pfs_read_user_file("/user/sudopass", &stored, &storedsz) ||
                !stored || storedsz == 0) {
                sh_printf("sudo: no password set - run 'sudo passwd' first\n");
                if (stored) kfree(stored);
                return;
            }
            if (!sudo_read_password("Password: ")) {
                sh_printf("sudo: cancelled\n"); kfree(stored); return;
            }
            bool ok = ((int)storedsz == g_pw_len);
            for (int i = 0; ok && i < g_pw_len; i++)
                if (g_pw_buf[i] != (char)stored[i]) ok = false;
            kfree(stored);
            if (!ok) { sh_printf("sudo: incorrect password\n"); return; }

            char subcmd_buf[MAX_COMMAND_LEN];
            int si = 0;
            while (subcmd[si] && si < MAX_COMMAND_LEN - 1) { subcmd_buf[si] = subcmd[si]; si++; }
            subcmd_buf[si] = 0;
            g_sudo_active = true;
            execute_command(subcmd_buf);
            g_sudo_active = false;
        }
        else {
            sh_printf("usage: sudo <command>\n       sudo passwd\n");
        }
    }
    else if (strlen(input) > 0) {
        sh_printf("Unknown command: %s\n", input);
    }
}

// --- Tab completion + history navigation helpers ---

static int g_esc_state = 0; // 0=normal 1=got ESC 2=got ESC+[

static const char* g_builtin_cmds[] = {
    "help", "ls", "cat", "rm", "mkdir", "chmod", "sudo", "pfs", "nettest", "netif", "netdevice",
    "ping", "nslookup", "wget", "clear", "run", "nano", "vim", "cd",
    0
};

static bool sh_prefix_match(const char* s, const char* prefix, int plen) {
    for (int i = 0; i < plen; i++) {
        if (!s[i] || s[i] != prefix[i]) return false;
    }
    return true;
}

static void history_navigate(int delta) {
    if (g_history_count == 0) return;

    // Save current buffer before entering browse mode
    if (g_history_pos == -1 && delta > 0)
        memcpy(g_history_saved, command_buffer, MAX_COMMAND_LEN);

    int new_pos = g_history_pos + delta;

    // Past newest: restore saved buffer
    if (new_pos < 0) {
        if (cursor_pos > 0) sh_emit_csi(cursor_pos, 'D');
        shell_out_char('\x1B'); shell_out_char('['); shell_out_char('K');
        memcpy(command_buffer, g_history_saved, MAX_COMMAND_LEN);
        buffer_len = (int)strlen(command_buffer);
        cursor_pos = buffer_len;
        for (int i = 0; i < buffer_len; i++) shell_out_char(command_buffer[i]);
        g_history_pos = -1;
        return;
    }
    if (new_pos >= g_history_count) return; // can't go further back

    const char* entry = history_get(new_pos);
    if (!entry) return;

    if (cursor_pos > 0) sh_emit_csi(cursor_pos, 'D');
    shell_out_char('\x1B'); shell_out_char('['); shell_out_char('K');
    g_history_pos = new_pos;
    int i = 0;
    while (entry[i] && i < MAX_COMMAND_LEN - 1) { command_buffer[i] = entry[i]; i++; }
    command_buffer[i] = 0;
    buffer_len = i;
    cursor_pos = buffer_len;
    for (int j = 0; j < buffer_len; j++) shell_out_char(command_buffer[j]);
}

#define TAB_MAX_MATCHES 32

static void shell_do_tab_complete(void) {
    if (cursor_pos != buffer_len) return; // only complete at end of line

    // Find the start of the last word in the buffer
    int word_start = 0;
    for (int i = 0; i < buffer_len; i++)
        if (command_buffer[i] == ' ') word_start = i + 1;

    const char* prefix = command_buffer + word_start;
    int plen = buffer_len - word_start;
    bool completing_cmd = (word_start == 0);

    const char* matches[TAB_MAX_MATCHES];
    int nmatch = 0;

    if (completing_cmd) {
        for (int i = 0; g_builtin_cmds[i] && nmatch < TAB_MAX_MATCHES; i++) {
            if (sh_prefix_match(g_builtin_cmds[i], prefix, plen))
                matches[nmatch++] = g_builtin_cmds[i];
        }
    }

    // Always add initrd file matches (useful for cat/run/nano args)
    file_t* files = initrd_get_files();
    for (int i = 0; i < MAX_FILES && nmatch < TAB_MAX_MATCHES; i++) {
        if (!files[i].exists) continue;
        if (is_file_protected(files[i].name)) continue;
        if (sh_prefix_match(files[i].name, prefix, plen))
            matches[nmatch++] = files[i].name;
    }

    if (nmatch == 0) return;

    if (nmatch == 1) {
        const char* rest = matches[0] + plen;
        while (*rest && buffer_len < MAX_COMMAND_LEN - 1) {
            command_buffer[buffer_len++] = *rest;
            cursor_pos++;
            shell_out_char(*rest);
            rest++;
        }
        if (completing_cmd && buffer_len < MAX_COMMAND_LEN - 1) {
            command_buffer[buffer_len++] = ' ';
            cursor_pos++;
            shell_out_char(' ');
        }
        return;
    }

    // Multiple matches: extend by longest common prefix, then list if stuck
    int lcp = (int)strlen(matches[0]) - plen;
    for (int i = 1; i < nmatch && lcp > 0; i++) {
        int ml = (int)strlen(matches[i]) - plen;
        if (ml < lcp) lcp = ml;
        for (int j = 0; j < lcp; j++) {
            if (matches[0][plen + j] != matches[i][plen + j]) { lcp = j; break; }
        }
    }

    for (int j = 0; j < lcp && buffer_len < MAX_COMMAND_LEN - 1; j++) {
        char ch = matches[0][plen + j];
        command_buffer[buffer_len++] = ch;
        cursor_pos++;
        shell_out_char(ch);
    }

    if (lcp == 0) {
        sh_printf("\n");
        for (int i = 0; i < nmatch; i++) { sh_printf(matches[i]); sh_printf("  "); }
        sh_printf("\n");
        shell_print_prompt();
        for (int i = 0; i < buffer_len; i++) shell_out_char(command_buffer[i]);
    }
}

void shell_update(char c) {
    shell_check_click();

    // If an editor (or other raw handler) is active, forward all keys there.
    if (g_input_handler) {
        g_input_handler(c, g_input_handler_user);
        return;
    }

    // Escape sequence state machine: ESC → [ → A/B/C/D
    if (g_esc_state == 1) {
        if (c == '[') { g_esc_state = 2; return; }
        g_esc_state = 0; // unexpected character after ESC — discard
        return;
    }
    if (g_esc_state == 2) {
        g_esc_state = 0;
        if (c == 'A') { history_navigate(1);  return; }
        if (c == 'B') { history_navigate(-1); return; }
        if (c == 'C') { if (cursor_pos < buffer_len) { cursor_pos++; sh_emit_csi(1, 'C'); } return; }
        if (c == 'D') { if (cursor_pos > 0)          { cursor_pos--; sh_emit_csi(1, 'D'); } return; }
        return;
    }

    if (c == '\x1B') { g_esc_state = 1; return; }
    if (c == '\t')   { shell_do_tab_complete(); return; }

    if (c == '\x03') { // Ctrl+C
        g_history_pos = -1;
        if (g_command_running) {
            g_shell_interrupted = true;
            sh_printf("^C\n");
        } else {
            memset(command_buffer, 0, MAX_COMMAND_LEN);
            buffer_len = 0;
            cursor_pos = 0;
            sh_printf("^C\n");
            shell_print_prompt();
        }
        return;
    }

    if (c == '\n') {
        command_buffer[buffer_len] = '\0';
        history_push(command_buffer);
        g_history_pos = -1;
        sh_printf("\n");
        // Don't run the command here — we're inside the keyboard ISR with
        // interrupts disabled (interrupt gate 0x8E). Queue it for the main
        // loop so timer/mouse/compositor keep running during execution.
        if (!g_command_ready) {
            memcpy(g_pending_command, command_buffer, MAX_COMMAND_LEN);
            g_command_ready = true;
        }
        memset(command_buffer, 0, MAX_COMMAND_LEN);
        buffer_len = 0;
        cursor_pos = 0;
    } else if (c == '\b') {
        if (cursor_pos > 0) {
            if (cursor_pos == buffer_len) {
                // At end: simple erase
                cursor_pos--;
                buffer_len--;
                command_buffer[buffer_len] = 0;
                shell_out_char('\b');
            } else {
                // Mid-line: shift buffer left, redraw tail
                for (int i = cursor_pos - 1; i < buffer_len - 1; i++)
                    command_buffer[i] = command_buffer[i + 1];
                buffer_len--;
                cursor_pos--;
                command_buffer[buffer_len] = 0;
                sh_emit_csi(1, 'D');
                shell_out_char('\x1B'); shell_out_char('['); shell_out_char('K');
                for (int i = cursor_pos; i < buffer_len; i++) shell_out_char(command_buffer[i]);
                if (buffer_len > cursor_pos) sh_emit_csi(buffer_len - cursor_pos, 'D');
            }
        }
    } else {
        if (buffer_len < MAX_COMMAND_LEN - 1) {
            if (cursor_pos == buffer_len) {
                // Appending at end
                command_buffer[buffer_len++] = c;
                cursor_pos++;
                shell_out_char(c);
            } else {
                // Insert in middle: shift right, redraw tail
                for (int i = buffer_len; i > cursor_pos; i--)
                    command_buffer[i] = command_buffer[i - 1];
                command_buffer[cursor_pos] = c;
                buffer_len++;
                cursor_pos++;
                for (int i = cursor_pos - 1; i < buffer_len; i++) shell_out_char(command_buffer[i]);
                if (buffer_len > cursor_pos) sh_emit_csi(buffer_len - cursor_pos, 'D');
            }
        }
    }
}