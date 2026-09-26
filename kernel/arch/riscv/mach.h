#ifndef CHRIS_RISCV_MACH_H
#define CHRIS_RISCV_MACH_H

/* RISC-V 64 image is not linked into this x86-64 kernel.
 * The installer reads these numbers from SYS/ARCH/RISCV.CC.
 * A future port uses Sv39, a 4 KiB page, and the same ChrisC bytecode. */

#define CHRIS_ARCH_X86_64  1
#define CHRIS_ARCH_RISCV64 2
#define CHRIS_RISCV_PAGE   4096
#define CHRIS_RISCV_SATP_SV39 8

#endif
