#ifndef EDITOR_H
#define EDITOR_H

// Full-screen terminal text editor.
// Uses ANSI escape sequences for positioning; requires an ANSI-capable terminal.
// Keyboard input is intercepted via shell_set_input_handler().

// Open a file in nano-style mode (always insert, Ctrl+S/O save, Ctrl+X quit).
void editor_open_nano(const char* filename);

// Open a file in vim-style mode (modal: normal/insert/command).
void editor_open_vim(const char* filename);

#endif
