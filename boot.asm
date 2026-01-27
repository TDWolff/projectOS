bits 32
section .text

align 8
header_start:
    dd 0xE85250D6                ; magic
    dd 0                         ; arch 0 (i386)
    dd header_end - header_start ; len
    dd 0x100000000 - (0xE85250D6 + 0 + (header_end - header_start)) ; checksum
    dw 0
    dw 0
    dd 8
header_end:
extern kernel_main
global start

start:
    cli
    mov esp, stack_space

    call setup_page_tables
    call enable_paging

    lgdt [gdt64_descriptor]

    jmp 0x08:long_mode_start

setup_page_tables:
    mov eax, p3_table
    or eax, 0b11
    mov [p4_table], eax

    mov eax, p2_table
    or eax, 0b11
    mov [p3_table], eax

    mov ecx, 0
.map_p2:
    mov eax, 0x200000    ; 2MB
    mul ecx
    or eax, 0b10000011
    mov [p2_table + ecx * 8], eax

    inc ecx
    cmp ecx, 512         ; Map 512 entries (1GB total covered)
    jne .map_p2
    ret

enable_paging:
    mov eax, p4_table
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    ret

[bits 64]
long_mode_start:
    mov ax, 0
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call kernel_main

    hlt

section .rodata
gdt64:
    dq 0 ; Null
    ; Code (0x08)
    dw 0xFFFF, 0
    db 0, 0x9A, 0xAF, 0
    ; Data (0x10)
    dw 0xFFFF, 0
    db 0, 0x92, 0xCF, 0
gdt64_descriptor:
    dw $ - gdt64 - 1
    dd gdt64

section .bss
align 4096
p4_table:
    resb 4096
p3_table:
    resb 4096
p2_table:
    resb 4096
stack_bottom:
    resb 16384
stack_space: