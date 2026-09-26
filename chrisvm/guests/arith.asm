; Direct-boot guest for ChrisCPU. Linked at physical 0x1000.
; Prints OK, checks 10+20, a memory round-trip, and CALL/RET, then HLT.

bits 64
global _start
section .text
_start:
    mov dx, 0x3f8
    mov al, 'O'
    out dx, al
    mov al, 'K'
    out dx, al
    mov al, 10
    out dx, al
    mov rax, 10
    mov rbx, 20
    add rax, rbx
    cmp rax, 30
    jne fail
    mov rdi, 0x4000
    mov qword [rdi], rax
    mov rcx, qword [rdi]
    cmp rcx, 30
    jne fail
    call leaf
    cmp rax, 31
    jne fail
    hlt
fail:
    mov dx, 0x3f8
    mov al, 'F'
    out dx, al
    hlt
leaf:
    add rax, 1
    ret
