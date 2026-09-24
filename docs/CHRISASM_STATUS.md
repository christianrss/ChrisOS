# ChrisAsm status

Audit snapshot: `894aed92e48e764e2627ecfc2514684f50809f59`.
Source: `compiler/chrisasm/chrisasm.c`. Text cap 65536 bytes. Overflow
stops emitting bytes and the assemble function can still return success.
An unknown mnemonic returns success.

## Registers

`rax`, `rbx`, `rcx`, `rdx`, `rsi`, `rdi`.

No `r8`–`r15`. No `eax`/`ebx`/`ecx`/`edx`/`esi`/`edi`/`r8d`–`r15d`.
No `ax`/`bx`/`cx`/`dx`. No `al`/`bl`/`cl`/`dl`.

## Instructions that emit bytes

| Mnemonic | Encoding |
| --- | --- |
| `ret` | `c3` |
| `syscall` | `0f 05` |
| `push rax` | `50` |
| `pop rax` | `58` |
| `mov reg, imm64` | `48 b8+r` plus imm64 |
| `mov reg, reg` | `48 89 /r` |
| `call symbol` | `e8` plus disp32 of zero |

`call` stores the symbol as a defined text symbol at the displacement.
It does not emit a relocation.

## Accepted and ignored

`.text`. Blank lines, `#` comments, `;` comments. Any other mnemonic.

## Not implemented

`lea`, arithmetic and logical ops, shifts, `cmp`/`test`, `jmp` and the
conditional jumps, `cli`, `sti`, `hlt`, `int`, `iretq`, `sysretq`,
`in`, `out`, `lgdt`, `lidt`, `ltr`, `mov` to or from `cr0`/`cr2`/`cr3`/`cr4`,
`invlpg`, `cpuid`, `rdmsr`, `wrmsr`, `xchg`, `lock`, `fxsave`/`fxrstor`,
and SSE.

Addressing: `[rax]`, `[rax+8]`, `[rbp-16]`, `[rax+rcx*4]`, `symbol`,
`symbol+offset`.

Sections other than the single text buffer. Directives `global`,
`extern`, `align`, `byte`, `word`, `dword`, `qword`, `zero`.

`host-chrisasm-test` covers the mnemonics above. It does not cover the
kernel's real assembly files.
