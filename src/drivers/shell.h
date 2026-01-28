#ifndef SHELL_H
#define SHELL_H

void shell_init();
void shell_update(char c); // Called by keyboard handler
void shell_check_click();
void run_program(const char* filename);

#endif