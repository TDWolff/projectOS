; boot.asm - Maps 4GB of memory to prevent Framebuffer crashes
bits 32
section .text

align 8
header_start:
    dd 0xE85250D6                ; magic
    dd 0                         ; arch 0 (i386)
    dd header_end - header_start ; len
    dd 0x100000000 - (0xE85250D6 + 0 + (header_end - header_start)) ; checksum

    ; --- Framebuffer Tag ---
    align 8
    dw 5
    dw 0
    dd 20
    dd 1024
    dd 768
    dd 32

    ; --- Information Request ---
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

start:
    cli
    mov esp, stack_space

    call setup_page_tables
    call enable_paging

    lgdt [gdt64_descriptor]
    jmp 0x08:long_mode_start

setup_page_tables:
    ; 1. Map P4 entry 0 to P3 table
    mov eax, p3_table
    or eax, 0b11
    mov [p4_table], eax

    ; 2. Map P3 entries (0, 1, 2, 3) to cover 4GB
    ; Each P3 entry covers 1GB. We need 4 entries.
    
    ; P3[0] -> p2_table_0 (0GB - 1GB)
    mov eax, p2_table_0
    or eax, 0b11
    mov [p3_table + 0], eax

    ; P3[1] -> p2_table_1 (1GB - 2GB)
    mov eax, p2_table_1
    or eax, 0b11
    mov [p3_table + 8], eax

    ; P3[2] -> p2_table_2 (2GB - 3GB)
    mov eax, p2_table_2
    or eax, 0b11
    mov [p3_table + 16], eax

    ; P3[3] -> p2_table_3 (3GB - 4GB)
    mov eax, p2_table_3
    or eax, 0b11
    mov [p3_table + 24], eax

    ; 3. Fill all 4 P2 tables with 2MB huge pages
    ; We need to map 512 entries * 4 tables = 2048 entries total
    mov ecx, 0         ; Page counter (0 to 2048)
    
.map_loop:
    ; Calculate physical address: ecx * 2MB
    mov eax, 0x200000
    mul ecx
    or eax, 0b10000011 ; Present + Writable + Huge

    ; Figure out which table and which slot to write to
    ; This part is tricky in raw asm loop, so we'll unroll it slightly logic-wise
    ; or just rely on the fact that the tables are contiguous in memory!
    ; Since p2_table_0, _1, _2, _3 are declared next to each other in BSS, 
    ; we can treat them as one giant array starting at p2_table_0.
    
    mov [p2_table_0 + ecx * 8], eax

    inc ecx
    cmp ecx, 2048      ; 2048 * 2MB = 4GB
    jne .map_loop

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
p4_table:
    resb 4096
p3_table:
    resb 4096
; We allocate 4 tables consecutively so we can loop over them easily
p2_table_0:
    resb 4096
p2_table_1:
    resb 4096
p2_table_2:
    resb 4096
p2_table_3:
    resb 4096
stack_bottom:
    resb 16384
stack_space: