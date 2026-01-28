; boot.asm - Maps 8GB of memory for stability and graphical mode
bits 32
section .text

align 8
header_start:
    dd 0xE85250D6                ; magic (Multiboot2)
    dd 0                         ; arch 0 (i386)
    dd header_end - header_start ; len
    dd 0x100000000 - (0xE85250D6 + 0 + (header_end - header_start)) ; checksum

    ; --- Framebuffer Tag (Request 1024x768x32) ---
    align 8
    dw 5
    dw 0
    dd 20
    dd 1024
    dd 768
    dd 32

    ; --- Information Request (Ask for Framebuffer info) ---
    align 8
    dw 1
    dw 0
    dd 12
    dd 8

    ; --- End Tag ---
    align 8
    dw 0
    dw 0
    dd 8
header_end:

extern kernel_main
global start
global p4_table

start:
    cli
    mov esp, stack_space

    call setup_page_tables
    call enable_paging

    lgdt [gdt64_descriptor]
    jmp 0x08:long_mode_start

setup_page_tables:
    ; 1. Link P4 to P3 (Entry 0)
    mov eax, p3_table
    or eax, 0b11
    mov [p4_table], eax

    ; 2. Link FOUR P3 entries to FOUR P2 tables (Covers 4GB)
    mov eax, p2_table_0 
    or eax, 0b11
    mov [p3_table], eax

    mov eax, p2_table_1
    or eax, 0b11
    mov [p3_table + 8], eax

    mov eax, p2_table_2
    or eax, 0b11
    mov [p3_table + 16], eax

    mov eax, p2_table_3
    or eax, 0b11
    mov [p3_table + 24], eax

    ; 3. Fill the P2 tables with 2048 huge pages (4GB total)
    mov ecx, 0
.map_p2_loop:
    ; Calculate physical address: ecx * 2MB
    mov eax, 0x200000
    mul ecx
    or eax, 0b10000011 ; Flags (Present + Writable + Huge Page)
    
    mov [p2_table_0 + ecx * 8], eax 
    
    inc ecx
    cmp ecx, 2048      ; Map 2048 pages (4GB total)
    jne .map_p2_loop

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

    mov edi, ebx 
    call kernel_main
    hlt

section .rodata
gdt64:
    dq 0
    dw 0xFFFF, 0
    db 0, 0x9A, 0xAF, 0
    dw 0xFFFF, 0
    db 0, 0x92, 0xCF, 0
gdt64_descriptor:
    dw $ - gdt64 - 1
    dd gdt64

section .bss
align 4096
p4_table:   resb 4096
p3_table:   resb 4096

; Need four P2 tables (16KB total) to map 4GB
p2_table_0: resb 4096
p2_table_1: resb 4096
p2_table_2: resb 4096
p2_table_3: resb 4096

stack_bottom:
    resb 16384
stack_space: