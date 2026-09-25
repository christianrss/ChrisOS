# ChrisAsm status

Audit snapshot: `894aed92e48e764e2627ecfc2514684f50809f59`.
Source: `compiler/chrisasm/chrisasm.c`. Text cap 65536 bytes. Overflow
stops emitting bytes and the assemble function can still return success.
An unknown mnemonic returns success.

## Registers

`rax`, `rbx`, `rcx`, `rdx`, `rsi`, `rdi`.

No `r8`–`r15`. No `eax`/`ebx`/`ecx`/`edx`/`esi`/`edi`/`r8d`–`r15d`.
No `ax`/`bx`/`cx`/`dx`. No `al`/`bl`/`cl`/`dl`.

An unknown mnemonic fails the assemble. `call symbol` emits `e8` and an
`R_X86_64_PLT32` relocation to an undefined symbol. A later label with
the same name defines that symbol. `push` and `pop` accept `rax`–`rdi`
and `r8`–`r15`. A full text buffer fails instead of dropping bytes.

## Instructions that emit bytes

| Mnemonic | Encoding |
| --- | --- |
| `ret` | `c3` |
| `syscall` | `0f 05` |
| `push reg` | `50+r`, or `41 50+r` for `r8`–`r15` |
| `pop reg` | `58+r`, or `41 58+r` for `r8`–`r15` |
| `mov reg, imm64` | `48 b8+r` plus imm64 |
| `mov reg, reg` | `48 89 /r` |
| `call symbol` | `e8` plus a `R_X86_64_PLT32` relocation, addend -4 |

`call` stores the symbol as a defined text symbol at the displacement.
It does not emit a relocation.

## Accepted and ignored

`.text` and `global`. Blank lines, `#` comments, and `;` comments.
Any other mnemonic fails.

## Encodings added for the kernel subset

`host-kcc-test` checks the bytes. `host-chrisasm-test` does not list
every one of these.

| Mnemonic | Encoding |
| --- | --- |
| `cli` | `fa` |
| `sti` | `fb` |
| `hlt` | `f4` |
| `pause` | `f3 90` |
| `not r64` | `REX.W f7 /2` (`not rax` is `48 f7 d0`) |
| `in al, dx` | `ec` |
| `in ax, dx` | `66 ed` |
| `in eax, dx` | `ed` |
| `out dx, al` | `ee` |
| `out dx, ax` | `66 ef` |
| `out dx, eax` | `ef` |

A same-section `.L` branch is patched in the assembler. It is not a
ChrisO symbol. A `.L` label in another section stays a symbol so ChrisLd
can apply the relocation. `invlpg`, `iretq`, and `mov` to or from a
control register are still rejected.

## Not implemented

`int`, `iretq`, `sysretq`, `lgdt`, `lidt`, `ltr`, `mov` to or from
`cr0`/`cr2`/`cr3`/`cr4`, `invlpg`, `cpuid`, `rdmsr`, `wrmsr`, `xchg`,
`lock`, `fxsave`/`fxrstor`, and SSE. The older rows above this section
describe an earlier assembler. `lea`, arithmetic, `cmp`, and `jmp` do
emit bytes in the current source.

Addressing: `[rax]`, `[rax+8]`, `[rbp-16]`, `[rax+rcx*4]`, `symbol`,
`symbol+offset`.

Sections other than the single text buffer. Directives `global`,
`extern`, `align`, `byte`, `word`, `dword`, `qword`, `zero`.

`host-chrisasm-test` covers the mnemonics above. It does not cover the
kernel's real assembly files.
