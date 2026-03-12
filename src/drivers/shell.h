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

// Prints "<username> % " to the current output sink.
// The shell owns prompt logic; UI code should call this after wiring a sink.
void shell_print_prompt();

#endif