.section .text
.global _start
_start:
    la sp, stack_top
    call kmain
1:
    wfi
    j 1b

.global rv_trap
.align 2
rv_trap:
    csrr t0, scause
    li t1, 0x8000000000000005
    bne t0, t1, 2f
    la t0, g_ticks
    ld t1, 0(t0)
    addi t1, t1, 1
    sd t1, 0(t0)
    li t0, -1
    csrw 0x14d, t0
2:
    sret

.section .bss
.align 12
stack:
    .skip 16384
stack_top:

.section .sbss
.global g_ticks
g_ticks:
    .dword 0
