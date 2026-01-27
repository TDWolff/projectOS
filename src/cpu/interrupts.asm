[bits 64]

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push qword %1
    jmp isr_common
%endmacro

; Exceptions
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15

; Hardware IRQs
ISR_NOERRCODE 32 ; Timer
ISR_NOERRCODE 33 ; Keyboard

extern isr_handler
global load_idt

load_idt:
    lidt [rdi]
    sti
    ret

isr_common:
    ; Save all registers
    push rbp
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; --- Stack Alignment Magic ---
    ; We must ensure RSP is a multiple of 16 before calling C.
    mov rbp, rsp          ; Save original RSP in RBP
    push rbp              ; Push original RSP to stack (8 bytes)
    push qword [rbp]      ; Push again (another 8 bytes) to align to 16
    
    mov rdi, rbp          ; Pass original RSP (the registers_t struct) as 1st arg
    call isr_handler      ; Call the C code

    pop rax               ; Clean up alignment pushes
    pop rax
    mov rsp, rbp          ; Restore original RSP
    ; -----------------------------

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    pop rbp
    add rsp, 16           ; Clean up error code and int number
    iretq