# Native toolchain gap

KCC, ChrisAsm, ChrisO, and ChrisLd are a bootstrap sketch. They are enough
for a few hand-sized programs. They are not enough to compile
`kernel/metal/*.c` or to link `BIN/KERNEL.ELF` from real objects.

This is a source audit, not a claim that any kernel file already builds
with KCC.

## What the code does now

`compiler/kcc/kcc.c` parses a small statement subset and prints ChrisAsm
text. The visible statements are things like `return` of an integer and a
dedicated `outb` call. There is no type checker, no preprocessor, and no
struct layout. The output buffer is 32 KiB.

`compiler/chrisasm/chrisasm.c` assembles into one text buffer
(`ASM_TEXT_MAX` 65536) and tags bytes as `CHRISO_SEC_TEXT`. It is not a
general x86-64 assembler: full register sets, memory operands, addressing
modes, sections, and relocations are not covered.

`compiler/chrisld/chrisld.c` `chrisld_link` takes one `ChrisoImage` and
writes an ELF64 executable with a single `PT_LOAD`. The entry is the symbol
`kstart` or `main` when present. The function does not take a list of
objects, does not report undefined or duplicate symbols, and does not apply
a relocation table.

## What a real kernel build needs

| Piece | Needed for `BIN/KERNEL.ELF` | Present |
| --- | --- | --- |
| Freestanding C subset used by `kernel/metal` | integers, pointers, structs, arrays, control flow, `static`, includes | No |
| Register and memory operands the kernel actually emits | yes | No |
| `.text` `.rodata` `.data` `.bss` | yes | Text only, in any useful sense |
| Relocations | yes | No |
| Multiple objects, undefined and duplicate symbols | yes | No |
| ELF64 the loader already boots | yes, Limine loads the host-linked kernel | One `PT_LOAD` program, not the host kernel image |
| x86-64 SysV ABI for the calls the kernel makes | yes | Not defined as a tested ABI |

## Chris Kernel C profile

Do not grow KCC into a second GCC. Define a profile and add a feature only
when a real kernel file needs it. The profile is not accepted yet. The
first file to compile under it is still to be chosen, and that compile is
gate 1 of the internal kernel build. Gates 2 through 7 (subsystem, basic
metal, link, `BIN/KERNEL.ELF`, install, boot of that ELF) are not started.

Limine may remain the external bootloader. Removing Limine is not required
for SH5.

## Host dependency that remains

`makefile` links the kernel with the host `ld` (`-T kernel/metal/linker.ld`).
That link is SH0. Replacing it with ChrisLd is SH4/SH5 work and has no gate.
