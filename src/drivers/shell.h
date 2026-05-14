#ifndef SHELL_H
#define SHELL_H

#include "../include/types.h"

void shell_init();
void shell_update(char c); // Called by keyboard handler
void shell_check_click();

// Optional: redirect shell output (echo/prompt/messages) to a UI sink.
// If unset, shell will be logic-only and won't print.
void shell_set_output_sink(void (*putc_cb)(char c, void* user), void* user);

// Query the current output sink (may be NULL if no terminal is attached).
void shell_get_output_sink(void (**out_putc_cb)(char c, void* user), void** out_user);

void sh_putc(char c);
void sh_printf(const char* fmt, ...);

// Called from the main kernel loop (NOT the keyboard ISR) to run queued commands.
bool shell_has_pending_command(void);
void shell_run_pending_command(void);

// Entry point for the shell worker task (created via create_task in kernel_main).
void shell_worker_entry(void);

// Backwards-compatible name (keep existing callsites working).
void shell_out_str(const char* s);

// Prints "<username> % " to the current output sink.
// The shell owns prompt logic; UI code should call this after wiring a sink.
void shell_print_prompt();

#endif