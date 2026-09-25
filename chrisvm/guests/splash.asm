; Boot splash drawn by ChrisCPU into the linear framebuffer at 0x02000000.
; 640x480 XRGB8888. The background matches kstart's first gfx_clear.

bits 64
global _start

FB equ 0x02000000
WIDTH equ 640
HEIGHT equ 480

section .text
_start:
    cld
    mov rdi, FB
    mov rcx, WIDTH * HEIGHT
    mov eax, 0x00101828
    rep stosd

    mov r8d, 120
    mov r9d, 90
    mov r10d, 400
    mov r11d, 280
    mov r13d, 0x00141c2c
    call fill_rect

    mov r8d, 120
    mov r9d, 90
    mov r10d, 400
    mov r11d, 4
    mov r13d, 0x004c8dff
    call fill_rect

    mov r8d, 120
    mov r9d, 366
    mov r10d, 400
    mov r11d, 4
    call fill_rect

    mov r8d, 120
    mov r9d, 90
    mov r10d, 4
    mov r11d, 280
    call fill_rect

    mov r8d, 516
    mov r9d, 90
    mov r10d, 4
    mov r11d, 280
    call fill_rect

    lea rsi, [rel title]
    mov r8d, 208
    mov r9d, 140
    mov r12d, 4
    mov r13d, 0x00f4f7fb
    call draw_string

    lea rsi, [rel subtitle]
    mov r8d, 208
    mov r9d, 200
    mov r12d, 2
    mov r13d, 0x0090a4c0
    call draw_string

    mov r8d, 160
    mov r9d, 250
    mov r10d, 320
    mov r11d, 22
    mov r13d, 0x000c141c
    call fill_rect

    mov r8d, 164
    mov r9d, 254
    mov r10d, 190
    mov r11d, 14
    mov r13d, 0x004c8dff
    call fill_rect

    lea rsi, [rel foot]
    mov r8d, 264
    mov r9d, 300
    mov r12d, 2
    mov r13d, 0x00b7c7d6
    call draw_string

    mov dx, 0x3f8
    mov rsi, msg
.put:
    mov al, [rsi]
    test al, al
    jz .halt
    out dx, al
    inc rsi
    jmp .put
.halt:
    hlt

; r8 x, r9 y, r10 w, r11 h, r13 color
fill_rect:
    push r9
    push r11
.rows:
    test r11d, r11d
    jz .rows_out
    mov rax, r9
    mov rdx, r9
    shl rax, 9
    shl rdx, 7
    add rax, rdx
    add rax, r8
    shl rax, 2
    mov rdi, FB
    add rdi, rax
    mov ecx, r10d
    mov eax, r13d
    rep stosd
    inc r9d
    dec r11d
    jmp .rows
.rows_out:
    pop r11
    pop r9
    ret

; rsi string, r8 x, r9 y, r12 scale, r13 color
draw_string:
.str:
    mov bl, [rsi]
    test bl, bl
    jz .str_out
    call draw_char
    mov eax, r12d
    shl eax, 3
    add r8d, eax
    inc rsi
    jmp .str
.str_out:
    ret

; bl char. Preserves rsi, r8, r9, r12, r13.
draw_char:
    push rsi
    push r8
    push r9
    push r10
    push r14
    push rdx
    call find_glyph
    xor r14d, r14d
.crow:
    cmp r14d, 8
    je .cdone
    movzx edx, byte [rsi]
    xor r10d, r10d
.ccol:
    cmp r10d, 8
    je .cnext
    test dl, 0x80
    jz .cskip
    push rdx
    call plot_cell
    pop rdx
.cskip:
    shl dl, 1
    inc r10d
    jmp .ccol
.cnext:
    inc rsi
    inc r14d
    jmp .crow
.cdone:
    pop rdx
    pop r14
    pop r10
    pop r9
    pop r8
    pop rsi
    ret

; origin r8,r9 plus cell r10,r14, scaled by r12
plot_cell:
    push r8
    push r9
    push r10
    push r11
    mov eax, r10d
    imul eax, r12d
    add r8d, eax
    mov eax, r14d
    imul eax, r12d
    add r9d, eax
    mov r10d, r12d
    mov r11d, r12d
    call fill_rect
    pop r11
    pop r10
    pop r9
    pop r8
    ret

; bl = char, returns rsi at 8 bitmap bytes
find_glyph:
    lea rsi, [rel font]
.scan:
    mov al, [rsi]
    test al, al
    jz .miss
    cmp al, bl
    je .hit
    add rsi, 9
    jmp .scan
.hit:
    inc rsi
    ret
.miss:
    lea rsi, [rel blank]
    ret

title:
    db "CHRISOS", 0
subtitle:
    db "inicializando", 0
foot:
    db "CHRISVM", 0
msg:
    db "splash", 10, 0

blank:
    db 0, 0, 0, 0, 0, 0, 0, 0

font:
    db 'C', 0x7e, 0xc3, 0xc0, 0xc0, 0xc0, 0xc0, 0xc3, 0x7e
    db 'H', 0xc3, 0xc3, 0xc3, 0xff, 0xc3, 0xc3, 0xc3, 0xc3
    db 'R', 0xfc, 0xc3, 0xc3, 0xfc, 0xcc, 0xc6, 0xc3, 0xc3
    db 'I', 0xff, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xff
    db 'S', 0x7e, 0xc3, 0xc0, 0x7c, 0x06, 0x03, 0xc3, 0x7e
    db 'O', 0x7e, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x7e
    db 'h', 0x60, 0x60, 0x60, 0x7c, 0x66, 0x66, 0x66, 0x66
    db 'r', 0x00, 0x00, 0x6c, 0x76, 0x60, 0x60, 0x60, 0x60
    db 'i', 0x18, 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3c
    db 's', 0x00, 0x00, 0x3c, 0x60, 0x3c, 0x06, 0x66, 0x3c
    db 'n', 0x00, 0x00, 0x6c, 0x76, 0x66, 0x66, 0x66, 0x66
    db 'c', 0x00, 0x00, 0x3c, 0x60, 0x60, 0x60, 0x66, 0x3c
    db 'a', 0x00, 0x00, 0x3c, 0x06, 0x3e, 0x66, 0x66, 0x3e
    db 'l', 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x36, 0x1c
    db 'z', 0x00, 0x00, 0x7e, 0x0c, 0x18, 0x30, 0x60, 0x7e
    db 'd', 0x06, 0x06, 0x3e, 0x66, 0x66, 0x66, 0x66, 0x3e
    db 'o', 0x00, 0x00, 0x3c, 0x66, 0x66, 0x66, 0x66, 0x3c
    db 'V', 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x66, 0x3c, 0x18
    db 'M', 0xc3, 0xe7, 0xff, 0xdb, 0xc3, 0xc3, 0xc3, 0xc3
    db 0
