[bits 64]
default rel

section .text
extern irq_dispatch
global isr_stub_table

%macro ISR_NO_ERROR 1
isr%1:
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%macro ISR_WITH_ERROR 1
isr%1:
    push qword %1
    jmp isr_common
%endmacro

%assign vector 0
%rep 256
%if vector = 8 || vector = 10 || vector = 11 || vector = 12 || vector = 13 || vector = 14 || vector = 17 || vector = 21 || vector = 29 || vector = 30
    ISR_WITH_ERROR vector
%else
    ISR_NO_ERROR vector
%endif
%assign vector vector + 1
%endrep

; irq_frame (rdi -> r15): r15..rbx, rax, vector@+120, error@+128, rip@+136
; rbx = frame pointer for the whole ISR (callee-saved, survives irq_dispatch).
isr_common:
    cld
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rbx, rsp
    sub rsp, 528
    lea rcx, [rsp + 16]
    and rcx, -16
    fxsave [rcx]
    mov qword [rbx - 528], rcx

    mov rdi, rbx
    mov r12, rsp
    and rsp, -16
    call irq_dispatch
    mov rsp, r12

    mov rcx, [rbx - 528]
    fxrstor [rcx]
    lea rsp, [rbx]

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq

global nmi_entry
extern mm_tlb_nmi_stop

; Vector 2. Runs on the interrupted stack so smp_current_cpu still sees
; the AP stack. An AP that missed the shootdown does not return. The BSP
; must return: halting it froze the desktop when a program closed.
nmi_entry:
    cld
    push rbx
    push rbp
    mov rbp, rsp
    and rsp, -16
    call mm_tlb_nmi_stop
    test eax, eax
    jnz .hang
    mov rsp, rbp
    pop rbp
    pop rbx
    iretq
.hang:
    cli
    hlt
    jmp .hang

section .rodata
align 8
isr_stub_table:
%assign vector 0
%rep 256
    dq isr%+vector
%assign vector vector + 1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
