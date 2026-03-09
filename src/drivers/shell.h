#ifndef SHELL_H
#define SHELL_H

void shell_init();
void shell_update(char c); // Called by keyboard handler
void shell_check_click();

// Optional: redirect shell output (echo/prompt/messages) to a UI sink.
// If unset, shell will be logic-only and won't print.
void shell_set_output_sink(void (*putc_cb)(char c, void* user), void* user);

#endif