bits 64
default rel
org 0x400000

ehdr:
    db 0x7F, "ELF", 2, 1, 1, 0
    times 8 db 0
    dw 2
    dw 62
    dd 1
    dq _start
    dq phdr - $$
    dq 0
    dd 0
    dw 64
    dw 56
    dw 1
    dw 0
    dw 0
    dw 0
phdr:
    dd 1
    dd 5
    dq 0
    dq 0x400000
    dq 0x400000
    dq filesize
    dq filesize
    dq 0x1000

_start:
    mov rax, [0]
    mov rax, 1
    xor rdi, rdi
    int 0x80
    hlt

filesize equ $ - $$
