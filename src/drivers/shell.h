#ifndef SHELL_H
#define SHELL_H

void shell_init();
void shell_update(char c); // Called by keyboard handler
void shell_check_click();

// Optional: redirect shell output (echo/prompt/messages) to a UI sink.
// If unset, shell will be logic-only and won't print.
void shell_set_output_sink(void (*putc_cb)(char c, void* user), void* user);

// Query the current output sink (may be NULL if no terminal is attached).
void shell_get_output_sink(void (**out_putc_cb)(char c, void* user), void** out_user);

// Print a string to the current shell output sink (typically terminal window).
// If no sink is attached, this is a no-op.
//
// Short aliases:
//   - sh_putc(c)    : emit one character
//   - sh_printf(str): emit a NUL-terminated string (no formatting)
//
// NOTE: These are intentionally *not* named kprintf/kprint_char to avoid
// confusion with the kernel-wide VGA-backed logging helpers.
void sh_putc(char c);
void sh_printf(const char* s);

// Backwards-compatible name (keep existing callsites working).
void shell_out_str(const char* s);

// Prints "<username> % " to the current output sink.
// The shell owns prompt logic; UI code should call this after wiring a sink.
void shell_print_prompt();

#endif