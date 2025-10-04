[org 0x00]
[bits 16]

section code

.init:
    mov eax, 0x07c0
    mov ds, eax
    mov eax, 0xb800
    mov es, eax
    mov eax, 0          ; set eax to 0 -> i = 0
    mov ebx, 0          ; Index of the character in the string that we are printing.
    mov ecx, 0          ; Actual address of the character on the screen
    mov dl, 0           ; Store the actual value that we are printing to the screen

.clear:
    mov byte [es:eax], 0        ; Move blank character to current text address
    inc eax
    mov byte [es:eax], 0x40     ; Move the background colour and character colour to the next address
    inc eax

    cmp eax, 2 * 25 * 80

    jl .clear

mov eax, text

.print:
    mov dl, byte [eax + ebx]
    cmp dl, 0
    je .print_end

    mov byte [es:ecx], dl
    inc ebx
    inc ecx
    inc ecx

    jmp .print

.print_end:
    mov eax, 0

jmp $

text: db 'Hello, World!', 0
text1: db 'This is another text!', 0

times 510 - ($ - $$) db 0x00    ; Pads the file with 0s, making it the right size

db 0x55
db 0xaa